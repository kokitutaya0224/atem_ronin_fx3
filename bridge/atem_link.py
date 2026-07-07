"""ATEM接続モジュール

役割:
  1. タリー監視 (PGM/PVW) — ME1のprogram/preview入力から算出
  2. カメラコントロール(CCU)の横取り — ATEM Software Controlの
     「カメラ」ページで行った操作をポーリング差分で検出し、コールバックで通知

PyATEMMaxが未インストール/未接続でも他の機能（ジンバル操作等）が
動くよう、接続失敗はすべて握りつぶしてログに出すだけにしている。
"""
import threading
import time
import logging

log = logging.getLogger("atem")

try:
    import PyATEMMax
    HAS_PYATEMMAX = True
except ImportError:
    HAS_PYATEMMAX = False

# PyATEMMax 1.0b9 の cameraControl 構造に合わせたアクセサ
# (iris/focus/whiteBalance/shutterは直値、gain/zoomはネストオブジェクト)
CCU_FIELDS = {
    "iris":         lambda cc: cc.iris,          # 生値 (実機で要スケール確認)
    "focus":        lambda cc: cc.focus,
    "gain":         lambda cc: cc.gain.value,    # センサーゲイン
    "whiteBalance": lambda cc: cc.whiteBalance,  # ケルビン
    "shutter":      lambda cc: cc.shutter,       # 露光時間
    "zoom":         lambda cc: cc.zoom.speed,    # ズーム速度
}

POLL_INTERVAL = 0.1  # 10Hz


class AtemLink:
    def __init__(self, ip, camera_ids, on_tally=None, on_ccu=None):
        """
        ip:         ATEMのIPアドレス
        camera_ids: 監視するカメラ番号(=ATEM入力番号)のリスト [1, 2, ...]
        on_tally:   callback(tally_dict)  例 {1: "pgm", 2: "pvw", 3: "off"}
        on_ccu:     callback(cam, field, value)
        """
        self.ip = ip
        self.camera_ids = camera_ids
        self.on_tally = on_tally
        self.on_ccu = on_ccu
        self.connected = False
        self.tally = {c: "off" for c in camera_ids}
        self._sw = None
        self._last_ccu = {}
        self._available_fields = None

    def start(self):
        if not HAS_PYATEMMAX:
            log.warning("PyATEMMax未インストール — ATEM連携は無効です")
            return
        self._sw = PyATEMMax.ATEMMax()
        self._sw.connect(self.ip)
        threading.Thread(target=self._loop, daemon=True, name="atem-poll").start()
        log.info("ATEM %s へ接続開始", self.ip)

    # ---- 内部 ----

    def _loop(self):
        while True:
            try:
                self.connected = bool(self._sw.connected)
                if self.connected:
                    self._poll_tally()
                    self._poll_ccu()
            except Exception as e:
                log.debug("ATEM poll error: %s", e)
            time.sleep(POLL_INTERVAL)

    def _poll_tally(self):
        try:
            pgm = int(self._sw.programInput[0].videoSource)
            pvw = int(self._sw.previewInput[0].videoSource)
        except Exception:
            return
        new = {}
        for c in self.camera_ids:
            if c == pgm:
                new[c] = "pgm"
            elif c == pvw:
                new[c] = "pvw"
            else:
                new[c] = "off"
        if new != self.tally:
            self.tally = new
            if self.on_tally:
                self.on_tally(new)

    def _poll_ccu(self):
        cc_root = getattr(self._sw, "cameraControl", None)
        if cc_root is None:
            if self._available_fields is None:
                self._available_fields = []
                log.warning("このPyATEMMaxにcameraControlが見つかりません。"
                            "CCU横取りは無効（タリーのみ動作）")
            return
        if self._available_fields is None:
            self._available_fields = list(CCU_FIELDS)
            log.info("CCU監視フィールド: %s", self._available_fields)
        for cam in self.camera_ids:
            try:
                cc = cc_root[cam]
            except Exception:
                continue
            for field, getter in CCU_FIELDS.items():
                try:
                    val = getter(cc)
                except Exception:
                    continue
                key = (cam, field)
                if self._last_ccu.get(key) != val:
                    first = key not in self._last_ccu
                    self._last_ccu[key] = val
                    # 初回は現状値の取り込みだけ（カメラへ流さない）
                    if not first and self.on_ccu:
                        self.on_ccu(cam, field, val)
