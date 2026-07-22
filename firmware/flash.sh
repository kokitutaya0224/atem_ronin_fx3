#!/usr/bin/env bash
# ESP32-POEへのファームウェア書き込み（カメラ番号を渡すだけでIP設定込みで完結）
#
#   ./flash.sh <カメラ番号>
#
# カメラ番号Nに対して固定IPを 192.168.10.(50+N) に自動設定する
# （bridge/config.json のデフォルトのgimbal IP割り当てと一致）。
set -e
cd "$(dirname "$0")"

CAM_NUM="$1"
if [ -z "$CAM_NUM" ]; then
  echo "使い方: ./flash.sh <カメラ番号>  (例: ./flash.sh 1)"
  exit 1
fi

if ! [[ "$CAM_NUM" =~ ^[0-9]+$ ]]; then
  echo "エラー: カメラ番号は数字で指定してください"
  exit 1
fi

LAST_OCTET=$((50 + CAM_NUM))
if [ "$LAST_OCTET" -gt 254 ]; then
  echo "エラー: カメラ番号が大きすぎます（IPが範囲外になります）"
  exit 1
fi

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO CLI (pio) が見つかりません。"
  read -rp "pip3 でインストールしますか？ [Y/n]: " ans
  if [ -z "$ans" ] || [ "$ans" = "Y" ] || [ "$ans" = "y" ]; then
    pip3 install -U platformio
  else
    echo "https://platformio.org/install/cli を参照して手動インストールしてください"
    exit 1
  fi
fi

echo "→ カメラ${CAM_NUM}用に固定IPを 192.168.10.${LAST_OCTET} に設定"
sed -i.bak -E "s/#define STATIC_IP[[:space:]]+192, 168, 10, [0-9]+/#define STATIC_IP     192, 168, 10, ${LAST_OCTET}/" src/config.h
rm -f src/config.h.bak

echo "→ ビルド & 書き込み中（ESP32-POEをUSBで接続しておくこと）..."
pio run -e esp32-poe -t upload

echo ""
echo "書き込み完了。bridge/config.json の gimbals.${CAM_NUM}.ip も"
echo "192.168.10.${LAST_OCTET} になっていることを確認してください（デフォルトのままなら一致済み）。"
echo "シリアルモニタで動作確認する場合: pio device monitor"
