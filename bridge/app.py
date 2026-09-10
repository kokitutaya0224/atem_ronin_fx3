"""ATEM PTZ Bridge — メインサーバー

起動:  python app.py
       → http://localhost:8090 で操作パネルが開ける

構成:
  ATEM ──(LAN/PyATEMMax)──> このアプリ ──(UDP)──> ESP32 ──(CAN)──> Ronin RS4 Pro
                              │  └──(subprocess)──> fx3_wrapper ──(USB)──> FX3
                              └──(WebSocket)──> 操作パネル(ブラウザ)
"""
import json
import logging
import os
import threading
import time

from flask import Flask, send_from_directory
from flask_sock import Sock

from atem_link import AtemLink
from camera_link import CameraManager
from gimbal_link import GimbalLink

logging.basicConfig(level=logging.INFO,
                    format="%(asctime)s %(name)-7s %(levelname)-5s %(message)s")
log = logging.getLogger("app")

HERE = os.path.dirname(os.path.abspath(__file__))
# 環境変数BRIDGE_CONFIGで設定ファイルを差し替え可能（テスト用）
config_path = os.environ.get("BRIDGE_CONFIG", os.path.join(HERE, "config.json"))
with open(config_path) as f:
    CONFIG = json.load(f)

app = Flask(__name__, static_folder=os.path.join(HERE, "static"))
sock = Sock(app)

clients = set()           # 接続中のWebSocket
clients_lock = threading.Lock()

# ---- 各リンクの初期化 -------------------------------------------------

gimbals = {
    int(gid): GimbalLink(gid, g["ip"], g["port"],
                         g.get("max_speed_dps", 90), g.get("expo", 0.5))
    for gid, g in CONFIG.get("gimbals", {}).items()
}

cameras = CameraManager(CONFIG.get("cameras", {}))

def on_tally(tally):
    log.info("タリー変化: %s", tally)
    broadcast({"t": "tally", "tally": tally})

def on_ccu(cam, field, value):
    log.info("CCU: cam%s %s=%s", cam, field, value)
    translated = cameras.handle_ccu(cam, field, value)
    # ATEM Software Control側での操作をWebパネルのUIにも反映させる
    if translated:
        broadcast({"t": "ccu", "cam": cam,
                   "param": translated[0], "value": translated[1]})

cam_ids = sorted(set(list(gimbals.keys()) + list(cameras.cams.keys())))
atem = AtemLink(CONFIG["atem_ip"], cam_ids, on_tally=on_tally, on_ccu=on_ccu)
atem.start()

# ---- WebSocket --------------------------------------------------------

def broadcast(obj):
    data = json.dumps(obj)
    with clients_lock:
        dead = set()
        for ws in clients:
            try:
                ws.send(data)
            except Exception:
                dead.add(ws)
        clients.difference_update(dead)

def state_snapshot():
    return {
        "t": "state",
        "atem_connected": atem.connected,
        "tally": atem.tally,
        "cameras": cameras.states(),
        "gimbals": {gid: {"angles": g.angles, "presets": g.list_presets()}
                    for gid, g in gimbals.items()},
    }

def state_loop():
    """5Hzで全クライアントへ状態を配信 + 角度を定期取得"""
    tick = 0
    while True:
        time.sleep(0.2)
        tick += 1
        if tick % 10 == 0:  # 2秒ごとに角度更新要求
            for g in gimbals.values():
                g.request_angles()
        broadcast(state_snapshot())

threading.Thread(target=state_loop, daemon=True).start()

@sock.route("/ws")
def ws_handler(ws):
    with clients_lock:
        clients.add(ws)
    ws.send(json.dumps(state_snapshot()))
    try:
        while True:
            raw = ws.receive()
            if raw is None:
                break
            handle_message(json.loads(raw))
    finally:
        with clients_lock:
            clients.discard(ws)

def handle_message(msg):
    t = msg.get("t")
    cam = int(msg.get("cam", 1))
    g = gimbals.get(cam)
    if t == "joy" and g:
        g.joystick(float(msg["yaw"]), float(msg["pitch"]))
    elif t == "recenter" and g:
        g.recenter(int(msg.get("ms", 1500)))
    elif t == "preset_save" and g:
        ok = g.save_preset(int(msg["slot"]))
        broadcast({"t": "toast",
                   "msg": f"プリセット{msg['slot']} 保存" if ok
                          else "保存失敗（角度未取得）"})
    elif t == "preset_recall" and g:
        g.recall_preset(int(msg["slot"]), int(msg.get("ms", 2000)))
    elif t == "cam_set":
        cameras.set_param(cam, msg["param"], msg["value"])
    elif t == "rec":
        cameras.rec(cam, bool(msg["on"]))
    elif t == "rec_all":
        cameras.rec_all(bool(msg["on"]))

# ---- HTTP -------------------------------------------------------------

@app.route("/")
def index():
    return send_from_directory(app.static_folder, "index.html")

if __name__ == "__main__":
    port = CONFIG.get("web_port", 8090)
    log.info("操作パネル: http://localhost:%d", port)
    app.run(host="0.0.0.0", port=port, threaded=True)
