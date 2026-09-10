#!/usr/bin/env bash
# 現場で使ったMacを元(DHCP)に戻す。setup-mac.sh で設定した有線ポートを自動判定。
set -euo pipefail
cd "$(dirname "$0")"

wifi_dev=$(networksetup -listallhardwareports | awk '/Hardware Port: Wi-Fi/{getline; print $2}')

# すべての有線ポートを見て、手動設定されているものをDHCPに戻す
devs=()
while IFS= read -r d; do
  [ -n "$d" ] && devs+=("$d")
done < <(
  networksetup -listallhardwareports | awk '/^Device: en/{print $2}' | while read -r dev; do
    [ "$dev" = "$wifi_dev" ] && continue
    echo "$dev"
  done
)

changed=0
for dev in "${devs[@]}"; do
  service=$(networksetup -listnetworkserviceorder | awk -v d="Device: ${dev})" '
    /^\([0-9]+\) /{name=$0; sub(/^\([0-9]+\) /,"",name)}
    $0 ~ d {print name; exit}')
  service="${service#\*}"
  [ -z "$service" ] && continue
  # 手動設定されている有線サービスだけ DHCP に戻す
  if networksetup -getinfo "$service" 2>/dev/null | grep -q "Manual Configuration"; then
    echo "→ ${service} (${dev}) を DHCP に戻します（管理者パスワードを求められます）"
    sudo networksetup -setdhcp "$service"
    changed=1
  fi
done

[ -f .last_mac_ip ] && rm -f .last_mac_ip
if [ "$changed" -eq 0 ]; then
  echo "手動設定の有線ポートは見つかりませんでした（既にDHCPのようです）。"
else
  echo "完了。"
fi
