#!/usr/bin/env bash
# ブリッジ起動（2回目以降はこれだけでOK）
# - このPCのブラウザで操作パネルを自動で開く
# - 同一LAN上の他PCから接続するためのURLも表示する（サーバーは 0.0.0.0 で待受）
set -e
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  echo "初回セットアップが未実施です。先に ./install.sh を実行してください。"
  exit 1
fi

source .venv/bin/activate

PORT=$(python3 -c "import json;print(json.load(open('config.json')).get('web_port',8090))" 2>/dev/null || echo 8090)

echo ""
echo "操作パネル:"
echo "  このPC        → http://localhost:${PORT}"
# 非ループバックのIPv4を全部出す。他PCは自分が到達できるアドレスを選ぶ
for ip in $(ifconfig 2>/dev/null | awk '/inet /{print $2}' | grep -v '^127\.'); do
  echo "  他PC(同一LAN) → http://${ip}:${PORT}"
done
echo "  ※起動まで数十秒（ATEM接続待ちのあいだブラウザは開けません）"
echo ""

# サーバーが listen したらこのPCの既定ブラウザで開く（バックグラウンド監視）
(
  for _ in $(seq 1 90); do
    if curl -s -o /dev/null "http://localhost:${PORT}/" 2>/dev/null; then
      command -v open >/dev/null && open "http://localhost:${PORT}"
      break
    fi
    sleep 1
  done
) &

python3 app.py
