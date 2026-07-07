"""ジンバル(ESP32ブリッジ)との通信モジュール

ESP32とはUDP/JSONで通信する:
  送信: {"t":"spd","y":<0.1deg/s>,"p":...,"r":0}   速度指令
        {"t":"pos","y":<0.1deg>,"p":...,"r":0,"ms":<移動時間ms>}
        {"t":"gp"}                                   角度取得要求
  受信: {"t":"ang","y":<0.1deg>,"p":...,"r":...}    角度応答

安全設計:
  - ジョイスティック入力が250ms途切れたらブリッジ側から速度0を送る
    （ESP32側にも独自の400msウォッチドッグがあり二重に守る）
"""
import json
import logging
import math
import os
import socket
import threading
import time

log = logging.getLogger("gimbal")

PRESET_FILE = os.path.join(os.path.dirname(__file__), "presets.json")
JOY_TIMEOUT = 0.25   # ジョイスティック無入力とみなす時間
SEND_RATE = 0.05     # 速度指令の再送周期 (20Hz)


class GimbalLink:
    def __init__(self, gimbal_id, ip, port, max_speed_dps=90, expo=0.5):
        self.id = str(gimbal_id)
        self.addr = (ip, port)
        self.max_speed = max_speed_dps
        self.expo = expo            # 0=リニア, 1=フルエクスポ
        self.angles = None          # {"y":.., "p":.., "r":..} 0.1deg
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(0.5)
        self._joy = (0.0, 0.0)      # (yaw, pitch) -1..1
        self._joy_time = 0.0
        self._active = False
        self._lock = threading.Lock()
        threading.Thread(target=self._tx_loop, daemon=True).start()
        threading.Thread(target=self._rx_loop, daemon=True).start()

    # ---- 公開API ----

    def joystick(self, yaw, pitch):
        """パネル/ゲームパッドからの入力 (-1..1)"""
        with self._lock:
            self._joy = (max(-1, min(1, yaw)), max(-1, min(1, pitch)))
            self._joy_time = time.time()
            self._active = True

    def recenter(self):
        self._send({"t": "pos", "y": 0, "p": 0, "r": 0, "ms": 1500})

    def move_to(self, yaw_deg, pitch_deg, ms=2000):
        self._send({"t": "pos", "y": int(yaw_deg * 10),
                    "p": int(pitch_deg * 10), "r": 0, "ms": ms})

    def request_angles(self):
        self._send({"t": "gp"})

    def save_preset(self, slot):
        """現在角度をプリセット保存。角度が未取得なら取得を試みてから保存"""
        if self.angles is None:
            self.request_angles()
            time.sleep(0.3)
        if self.angles is None:
            return False
        presets = _load_presets()
        presets.setdefault(self.id, {})[str(slot)] = self.angles
        _save_presets(presets)
        log.info("gimbal%s preset%s 保存: %s", self.id, slot, self.angles)
        return True

    def recall_preset(self, slot, ms=2000):
        presets = _load_presets().get(self.id, {})
        p = presets.get(str(slot))
        if not p:
            return False
        self._send({"t": "pos", "y": p["y"], "p": p["p"], "r": 0, "ms": ms})
        return True

    def list_presets(self):
        return sorted(_load_presets().get(self.id, {}).keys())

    # ---- 内部 ----

    def _curve(self, v):
        """エクスポカーブ: 小入力を細かく、大入力は素早く"""
        return (1 - self.expo) * v + self.expo * (v ** 3)

    def _tx_loop(self):
        """速度指令の定期送信。無入力になったら速度0を1回送って停止"""
        while True:
            time.sleep(SEND_RATE)
            with self._lock:
                joy, jt, active = self._joy, self._joy_time, self._active
            if not active:
                continue
            if time.time() - jt > JOY_TIMEOUT:
                self._send({"t": "spd", "y": 0, "p": 0, "r": 0})
                with self._lock:
                    self._active = False
                continue
            y = self._curve(joy[0]) * self.max_speed
            p = self._curve(joy[1]) * self.max_speed
            self._send({"t": "spd", "y": int(y * 10), "p": int(p * 10), "r": 0})

    def _rx_loop(self):
        while True:
            try:
                data, _ = self._sock.recvfrom(512)
                msg = json.loads(data)
                if msg.get("t") == "ang":
                    self.angles = {"y": msg["y"], "p": msg["p"], "r": msg.get("r", 0)}
            except socket.timeout:
                continue
            except Exception as e:
                log.debug("gimbal rx error: %s", e)

    def _send(self, obj):
        try:
            self._sock.sendto(json.dumps(obj).encode(), self.addr)
        except OSError as e:
            log.debug("gimbal tx error: %s", e)


def _load_presets():
    try:
        with open(PRESET_FILE) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _save_presets(presets):
    with open(PRESET_FILE, "w") as f:
        json.dump(presets, f, indent=2)
