"""FX3制御モジュール

実カメラは fx3_wrapper (Sony Camera Remote SDKのC++ラッパー) を
サブプロセスとして起動し、JSON Lines (1行1コマンド) で通信する。
  bridge → wrapper: {"cmd":"set_iris_norm","value":0.5}
  wrapper → bridge: {"ok":true} / {"event":"status","rec":true,...}

mode="mock" のときはログ出力のみのモックで動作し、
ATEM/パネル→ブリッジの経路をカメラ実機なしで検証できる。
"""
import json
import logging
import os
import subprocess
import threading

log = logging.getLogger("camera")

# ATEMのCCUフィールド → ラッパーコマンドへの変換
# ATEM側の値スケールは実機で要キャリブレーション（README参照）
GAIN_DB_TO_ISO = {0: 800, 6: 1600, 12: 3200, 18: 6400}  # FX3 base ISO 800


class BaseCamera:
    def set_param(self, param, value): ...
    def rec(self, on): ...
    @property
    def state(self): return {}


class MockCamera(BaseCamera):
    def __init__(self, cam_id):
        self.id = cam_id
        self._state = {"rec": False, "connected": True, "mode": "mock"}

    def set_param(self, param, value):
        self._state[param] = value
        log.info("[mock cam%s] %s = %s", self.id, param, value)

    def rec(self, on):
        self._state["rec"] = bool(on)
        log.info("[mock cam%s] REC %s", self.id, "開始" if on else "停止")

    @property
    def state(self):
        return dict(self._state)


class Fx3Camera(BaseCamera):
    def __init__(self, cam_id, wrapper_path, serial=""):
        self.id = cam_id
        self._state = {"rec": False, "connected": False, "mode": "fx3"}
        self._lock = threading.Lock()
        self._proc = None
        path = os.path.abspath(
            os.path.join(os.path.dirname(__file__), wrapper_path))
        try:
            self._proc = subprocess.Popen(
                [path, "--serial", serial] if serial else [path],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
            threading.Thread(target=self._read_loop, daemon=True).start()
            self._send({"cmd": "connect"})
        except OSError as e:
            log.error("fx3_wrapper起動失敗 (%s): %s — READMEのビルド手順参照", path, e)

    def _send(self, obj):
        if not self._proc or self._proc.poll() is not None:
            return
        with self._lock:
            self._proc.stdin.write(json.dumps(obj) + "\n")
            self._proc.stdin.flush()

    def _read_loop(self):
        for line in self._proc.stdout:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("event") == "status":
                self._state.update({k: v for k, v in msg.items() if k != "event"})

    def set_param(self, param, value):
        self._send({"cmd": f"set_{param}", "value": value})

    def rec(self, on):
        self._send({"cmd": "rec", "value": bool(on)})
        self._state["rec"] = bool(on)

    @property
    def state(self):
        return dict(self._state)


class CameraManager:
    def __init__(self, config):
        self.cams = {}
        for cam_id, c in config.items():
            cid = int(cam_id)
            if c.get("mode") == "fx3":
                self.cams[cid] = Fx3Camera(cid, c["wrapper"], c.get("serial", ""))
            else:
                self.cams[cid] = MockCamera(cid)

    def set_param(self, cam, param, value):
        if cam in self.cams:
            self.cams[cam].set_param(param, value)

    def rec(self, cam, on):
        if cam in self.cams:
            self.cams[cam].rec(on)

    def rec_all(self, on):
        for c in self.cams.values():
            c.rec(on)

    def states(self):
        return {cid: c.state for cid, c in self.cams.items()}

    def handle_ccu(self, cam, field, value):
        """ATEMのCCU操作をカメラコマンドへ翻訳する

        戻り値: 変換後の (param, value)。カメラへ流さなかった場合は None。
        呼び出し側はこれをWebパネルへブロードキャストしてUIを同期する。
        """
        if cam not in self.cams:
            return None
        param = v = None
        try:
            if field == "iris":
                # ATEM側は正規化値想定。生値(0-2048等)なら実機ログを見て要調整
                v = float(value)
                if v > 1.0:
                    v = v / 2048.0
                param, v = "iris_norm", round(v, 4)
            elif field == "whiteBalance":
                param, v = "wb_kelvin", int(value)
            elif field == "shutter":
                # ATEMはマイクロ秒 → 1/x秒へ
                us = int(value)
                if us > 0:
                    param, v = "shutter", int(round(1_000_000 / us))
            elif field == "gain":
                iso = GAIN_DB_TO_ISO.get(int(value))
                if iso:
                    param, v = "iso", iso
            elif field == "focus":
                param, v = "focus_norm", float(value)
            elif field == "zoom":
                param, v = "zoom_speed", float(value)
        except (TypeError, ValueError) as e:
            log.debug("CCU変換スキップ %s=%s: %s", field, value, e)
            return None
        if param is None:
            return None
        self.set_param(cam, param, v)
        return (param, v)
