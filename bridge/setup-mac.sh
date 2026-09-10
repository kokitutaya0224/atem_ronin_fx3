#!/usr/bin/env bash
# 従業員のMacをこの現場の制御LANに参加させる（毎回・PCが変わる前提）
#   - 有線Ethernet(USB-LANアダプタ)に固定IPを設定
#   - config.json の ATEM IP を設定
# 使い終わったら ./reset-mac-net.sh でDHCPに戻せる。
set -euo pipefail
cd "$(dirname "$0")"

MASK="255.255.255.0"
SUBNET="192.168.10"
ESP32_IP="192.168.10.51"      # ファーム側で固定（firmware/src/config.h）
DEFAULT_MAC_IP="192.168.10.100"

echo "== 現場LANセットアップ =="
echo

# ---- 1. 有線Ethernetの口を特定 -------------------------------------------
# Wi-Fiのデバイス名を除外しつつ、ケーブルがリンクしている en* を探す
wifi_dev=$(networksetup -listallhardwareports | awk '/Hardware Port: Wi-Fi/{getline; print $2}')
candidates=()
while IFS= read -r c; do
  [ -n "$c" ] && candidates+=("$c")
done < <(
  networksetup -listallhardwareports | awk '/^Device: en/{print $2}' | while read -r dev; do
    [ "$dev" = "$wifi_dev" ] && continue
    if ifconfig "$dev" 2>/dev/null | grep -q "status: active"; then echo "$dev"; fi
  done
)

if [ "${#candidates[@]}" -eq 0 ]; then
  echo "⚠ ケーブルがリンクしている有線ポートが見つかりません。"
  echo "  USB-LANアダプタをMacに挿し、PoEスイッチへLANケーブルを繋いでから再実行してください。"
  exit 1
elif [ "${#candidates[@]}" -eq 1 ]; then
  dev="${candidates[0]}"
else
  echo "有線ポートが複数あります。使うものを選んでください:"
  select d in "${candidates[@]}"; do dev="$d"; [ -n "$dev" ] && break; done
fi

# デバイス名(enX) → networksetup のサービス名 に変換
service=$(networksetup -listnetworkserviceorder | awk -v d="Device: ${dev})" '
  /^\([0-9]+\) /{name=$0; sub(/^\([0-9]+\) /,"",name)}
  $0 ~ d {print name; exit}')
service="${service#\*}"   # 無効化サービスの先頭 * を除去
if [ -z "$service" ]; then
  echo "⚠ ${dev} に対応するネットワークサービス名を特定できませんでした。"
  echo "  システム設定 > ネットワーク で手動設定するか、管理者に連絡してください。"
  exit 1
fi
echo "使用する有線ポート: ${dev}  (サービス名: ${service})"
echo

# ---- 2. このMacのIPを入力 ----------------------------------------------
last_ip=""
[ -f .last_mac_ip ] && last_ip=$(cat .last_mac_ip)
prompt_default="${last_ip:-$DEFAULT_MAC_IP}"
read -rp "このMacに割り当てるIP [${prompt_default}]: " mac_ip
mac_ip="${mac_ip:-$prompt_default}"

octet="${mac_ip##*.}"
if [ "${mac_ip%.*}" != "$SUBNET" ] || ! [[ "$octet" =~ ^[0-9]+$ ]]; then
  echo "⚠ ${SUBNET}.x 形式で入力してください（例: ${DEFAULT_MAC_IP}）"
  exit 1
fi
if [ "$octet" -lt 2 ] || [ "$octet" -gt 238 ]; then
  echo "⚠ 末尾は 2〜238 にしてください（.1=ルータ想定 / .239=ATEM）"; exit 1
fi
if [ "$octet" -ge 51 ] && [ "$octet" -le 70 ]; then
  echo "⚠ .51〜.70 はジンバル(ESP32)用に予約済みです。別の値にしてください。"; exit 1
fi

# ---- 3. ATEMのIPを入力 -----------------------------------------------
cur_atem=$(python3 -c "import json;print(json.load(open('config.json')).get('atem_ip',''))" 2>/dev/null || echo "")
read -rp "ATEMのIP [${cur_atem:-192.168.10.239}]: " atem_ip
atem_ip="${atem_ip:-${cur_atem:-192.168.10.239}}"

# ---- 4. 適用 -------------------------------------------------------------
echo
echo "→ ${service} を ${mac_ip}/${MASK} に設定します（管理者パスワードを求められます）"
sudo networksetup -setmanual "$service" "$mac_ip" "$MASK"
echo "$mac_ip" > .last_mac_ip

if [ "$atem_ip" != "$cur_atem" ]; then
  python3 - "$atem_ip" <<'PYEOF'
import json, sys
with open("config.json") as f: cfg = json.load(f)
cfg["atem_ip"] = sys.argv[1]
with open("config.json", "w") as f: json.dump(cfg, f, indent=2, ensure_ascii=False)
print(f"→ config.json の atem_ip を {sys.argv[1]} に設定しました")
PYEOF
fi

# ---- 5. 疎通確認 ------------------------------------------------------
echo
echo -n "ESP32(${ESP32_IP}) へ ping ... "
if ping -c 2 -t 2 "$ESP32_IP" >/dev/null 2>&1; then
  echo "OK"
else
  echo "NG"
  echo "  ・LANケーブルがPoEスイッチに繋がっているか"
  echo "  ・ESP32(ジンバル側)の電源が入っているか"
  echo "  ・選んだ有線ポート(${dev})が正しいか"
  echo "  を確認してください。"
fi
echo -n "ATEM(${atem_ip}) へ ping ... "
ping -c 2 -t 2 "$atem_ip" >/dev/null 2>&1 && echo "OK" || echo "NG（ATEM未起動でもブリッジは起動できます）"

echo
echo "セットアップ完了。次は  ./start.sh  で操作パネルを開けます。"
echo "使い終わったら  ./reset-mac-net.sh  でこのMacをDHCPに戻せます。"
