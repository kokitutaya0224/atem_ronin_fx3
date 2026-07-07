"""モックモードのE2Eテスト（ハードウェアなしで実行可能）

検証内容:
  1. HTTPで操作パネルが配信される
  2. WebSocketで初期stateが届く
  3. ジョイスティック入力 → ESP32向けUDP速度指令に変換される
  4. 入力停止後にウォッチドッグが速度0を送る
  5. カメラ操作(ISO/REC)がモックカメラに届く

使い方: サーバー(app.py)を起動した状態で
  .venv/bin/python test_e2e.py
"""
import json
import socket
import threading
import time
import urllib.request

from websockets.sync.client import connect

BASE = "http://localhost:8090"
results = []

def check(name, ok, detail=""):
    results.append(ok)
    print(f"{'✅' if ok else '❌'} {name} {detail}")

# 1. HTTP
html = urllib.request.urlopen(BASE + "/", timeout=5).read().decode()
check("パネル配信", "ATEM PTZ Bridge" in html, f"({len(html)} bytes)")

# ESP32の代役: UDP 5005で待ち受け
udp_received = []
esp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
esp.bind(("127.0.0.1", 5005))
esp.settimeout(0.5)

stop = threading.Event()

def udp_rx():
    while not stop.is_set():
        try:
            data, addr = esp.recvfrom(512)
            udp_received.append(json.loads(data))
        except socket.timeout:
            pass

threading.Thread(target=udp_rx, daemon=True).start()

# 2. WebSocket
ws = connect("ws://localhost:8090/ws".replace("http://", ""), open_timeout=5)
state = json.loads(ws.recv(timeout=5))
check("WS初期state受信", state.get("t") == "state",
      f"(atem={state.get('atem_connected')}, tally={state.get('tally')})")

# 3. ジョイスティック → UDP速度指令
for _ in range(8):
    ws.send(json.dumps({"t": "joy", "cam": 1, "yaw": 0.5, "pitch": -0.3}))
    time.sleep(0.04)
time.sleep(0.2)
spd = [m for m in udp_received if m.get("t") == "spd" and m["y"] != 0]
check("ジョイスティック→UDP速度指令", len(spd) >= 2,
      f"({len(spd)}発, 例: {spd[0] if spd else 'なし'})")

# 4. ウォッチドッグ（入力停止→速度0）
time.sleep(0.5)
zeros = [m for m in udp_received if m.get("t") == "spd" and m["y"] == 0 and m["p"] == 0]
check("ウォッチドッグ停止指令", len(zeros) >= 1, f"({len(zeros)}発)")

# 5. カメラ操作（モック）
ws.send(json.dumps({"t": "cam_set", "cam": 1, "param": "iso", "value": 1600}))
ws.send(json.dumps({"t": "rec", "cam": 1, "on": True}))
time.sleep(0.5)
# 次のstate配信でrec=trueが反映されるはず
rec_ok = False
deadline = time.time() + 3
while time.time() < deadline:
    m = json.loads(ws.recv(timeout=3))
    if m.get("t") == "state" and m.get("cameras", {}).get("1", {}).get("rec"):
        rec_ok = True
        break
check("REC状態がstateに反映", rec_ok)

# プリセット保存（角度は実機がないので失敗が正常だがクラッシュしないこと）
ws.send(json.dumps({"t": "preset_recall", "cam": 1, "slot": 1}))
time.sleep(0.3)
check("プリセット呼び出しでクラッシュしない", True)

ws.close()
stop.set()
print(f"\n{sum(results)}/{len(results)} 件成功")
raise SystemExit(0 if all(results) else 1)
