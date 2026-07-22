#!/usr/bin/env bash
# ブリッジの初回セットアップ（venv作成 + 依存インストール + ATEM IP設定）
set -e
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  echo "→ Python仮想環境を作成中..."
  python3 -m venv .venv
fi

source .venv/bin/activate
echo "→ 依存パッケージをインストール中..."
pip install -q --upgrade pip
pip install -q -r requirements.txt

if [ -t 0 ]; then
  echo ""
  read -rp "ATEMのIPアドレスを入力してください（未入力ならスキップ）: " atem_ip
  if [ -n "$atem_ip" ]; then
    python3 - "$atem_ip" <<'PYEOF'
import json, sys
path = "config.json"
with open(path) as f:
    cfg = json.load(f)
cfg["atem_ip"] = sys.argv[1]
with open(path, "w") as f:
    json.dump(cfg, f, indent=2, ensure_ascii=False)
print(f"→ config.json の atem_ip を {sys.argv[1]} に設定しました")
PYEOF
  fi
fi

echo ""
echo "セットアップ完了。次回からは ./start.sh で起動できます。"
echo "起動しますか？ [Y/n]"
read -r ans
if [ -z "$ans" ] || [ "$ans" = "Y" ] || [ "$ans" = "y" ]; then
  exec ./start.sh
fi
