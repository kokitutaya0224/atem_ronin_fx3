// ATEM PTZ Bridge — ESP32ファームウェア
// LAN(有線Ethernet)またはWiFiのUDPで受けたコマンドをDJI R SDK (CAN)へ中継する
// ビルド環境で切替: esp32-poe=有線LAN(NET_ETH) / esp32dev=WiFi(NET_WIFI)
//
// UDPプロトコル (JSON, bridge/gimbal_link.py と対):
//   {"t":"spd","y":<0.1deg/s>,"p":...,"r":...}      速度指令
//   {"t":"pos","y":<0.1deg>,"p":...,"r":...,"ms":n} 位置指令
//   {"t":"gp"}                                       角度取得
//   応答 {"t":"ang","y":...,"p":...,"r":...}         (0.1deg)

#include <Arduino.h>
#ifdef NET_ETH
#include <ETH.h>   // Olimex ESP32-POE: LAN8720のピン定義はボードバリアントが持つ
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>  // UDPソケットはlwIP共通なので有線LANでもこのクラスを使う
#include <ArduinoJson.h>
#include "driver/twai.h"  // 起動時自己診断でバス状態を読むため
#include "driver/gpio.h"  // 同上。DC試験でピンをGPIOへ戻すのに使う
#include "config.h"
#include "dji_r_sdk.h"

WiFiUDP udp;
IPAddress lastClient;
uint16_t lastClientPort = 0;

int16_t spdYaw = 0, spdPitch = 0, spdRoll = 0;
bool moving = false;
uint32_t lastSpdCmd = 0;
uint32_t lastSpdSent = 0;

// ブリングアップ支援。Roninから一度も応答が無い間は1秒ごとに角度要求を投げ直し、
// バスオフからも自動復帰する。配線を差し替えながらシリアルで結果を追えるようにする。
bool roninSeen = false;
uint32_t lastProbe = 0;
int lastReportedTxErr = -1;

// ネットワーク未確立でもCANの状態が分かるよう、CANを先に初期化する。
// リンク待ちで停止すると現場でLANケーブル1本の抜けが無音のハングに化けるため、
// 待ちは打ち切って続行し、リンクは後から復帰させる。
#define LINK_WAIT_MS 15000

// 外部回路を完全に外して、ESP32のTWAIペリフェラルとピン自体の健全性を確かめる。
// TXとRXを同じピンに割り当てるとGPIOマトリクス内で信号が折り返すため、
// トランシーバも配線も介さずに送受信が成立するはず。ここが通ればESP32側は白。
static void canInternalLoopbackTest() {
    twai_stop();
    twai_driver_uninstall();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_TX_PIN, TWAI_MODE_NO_ACK);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
        Serial.println("ESP32内部ループバック: ドライバ構成に失敗");
        return;
    }

    twai_message_t tx = {};
    tx.identifier = 0x123;
    tx.self = 1;
    tx.data_length_code = 2;
    tx.data[0] = 0x5A;
    tx.data[1] = 0xC3;

    bool ok = false;
    if (twai_transmit(&tx, pdMS_TO_TICKS(100)) == ESP_OK) {
        twai_message_t rx;
        uint32_t deadline = millis() + 300;
        while (millis() < deadline) {
            if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK &&
                rx.identifier == 0x123 && rx.data[0] == 0x5A) { ok = true; break; }
        }
    }

    if (ok) {
        Serial.println("ESP32内部ループバックOK — TWAIペリフェラルとGPIO14は正常。ESP32側は問題なし");
        Serial.println("  → 原因はトランシーバ以降（モジュール本体/CANH-CANL配線）に確定");
    } else {
        Serial.println("ESP32内部ループバックNG — ESP32のTWAI設定かGPIO14自体に問題がある");
    }

    twai_stop();
    twai_driver_uninstall();
}

// トランシーバをCANプロトコル抜きのDCレベルで直接叩いて生死を見る。
// SN65HVD230は CTX=Low→バスdominant→CRX=Low、CTX=High→recessive→CRX=High と追従する。
// 追従しなければESP32とモジュールの間（結線/3.3V給電/Rsピン）が原因と確定できる。
// dominantタイムアウト（最低4kbps相当）に掛からないよう保持は数十usに留める。
static void canPinLevelTest() {
    twai_stop();
    twai_driver_uninstall();

    // twai_driver_uninstall()はGPIOマトリクスのルーティングを戻さない。
    // 明示的にリセットしないとピンがTWAIペリフェラルに繋がったままで、
    // digitalWrite()が効かず「CTXを振っても何も起きない」偽の結果になる。
    gpio_reset_pin(CAN_TX_PIN);
    gpio_reset_pin(CAN_RX_PIN);

    // まずCRXが本当に駆動されているかを見る。内部プルダウン(約45kΩ)に負けてLowへ
    // 落ちるなら、その線は誰も駆動していない＝モジュール未接続か無給電と分かる。
    // トランシーバのR出力はプッシュプルなので、生きていればプルダウンに勝ってHighのまま。
    pinMode(CAN_RX_PIN, INPUT_PULLDOWN);
    delay(2);
    int withPulldown = digitalRead(CAN_RX_PIN);
    pinMode(CAN_RX_PIN, INPUT_PULLUP);
    delay(2);
    int withPullup = digitalRead(CAN_RX_PIN);

    Serial.printf("CRX駆動判定: プルダウン時=%d プルアップ時=%d\n", withPulldown, withPullup);
    if (withPulldown == 0 && withPullup == 1) {
        Serial.println("  → CRX(GPIO13)は誰にも駆動されず浮いている＝モジュール未接続か無給電");
        Serial.println("     R端子とGPIO13の結線、モジュールのVCC(3.3V)とGND共通接続を最優先で確認");
        pinMode(CAN_RX_PIN, INPUT);
        dji::begin(CAN_TX_PIN, CAN_RX_PIN);
        return;
    }
    Serial.println("  → CRXは駆動されている（モジュールは給電済みで受信回路は生きている）");

    // CTX(GPIO14)がモジュールのD端子に届いているかを見る。SN65HVD230のD入力は
    // 内部プルアップを持つので、結線されていればESP32の内部プルダウンを掛けても
    // High側に引かれる。Lowに落ちるなら未結線の疑いが濃い。
    pinMode(CAN_TX_PIN, INPUT_PULLDOWN);
    delay(2);
    int txWithPulldown = digitalRead(CAN_TX_PIN);
    pinMode(CAN_TX_PIN, INPUT_PULLUP);
    delay(2);
    int txWithPullup = digitalRead(CAN_TX_PIN);
    Serial.printf("CTX結線判定: プルダウン時=%d プルアップ時=%d\n", txWithPulldown, txWithPullup);
    if (txWithPulldown == 1) {
        Serial.println("  → CTX(GPIO14)は外部でHighに引かれている＝D端子と結線されている");
    } else {
        Serial.println("  → CTX(GPIO14)に外部からの引きなし。D端子未結線の疑い（D内部プルアップが弱い場合も同じ表示になる）");
    }

    pinMode(CAN_TX_PIN, OUTPUT);
    pinMode(CAN_RX_PIN, INPUT);

    int highFollow = 0, lowFollow = 0;
    for (int i = 0; i < 8; i++) {
        digitalWrite(CAN_TX_PIN, HIGH);
        delayMicroseconds(100);
        if (digitalRead(CAN_RX_PIN) == HIGH) highFollow++;
        digitalWrite(CAN_TX_PIN, LOW);
        delayMicroseconds(50);
        if (digitalRead(CAN_RX_PIN) == LOW) lowFollow++;
        digitalWrite(CAN_TX_PIN, HIGH);
        delayMicroseconds(200);
    }

    Serial.printf("トランシーバDC試験: CTX=H→CRX=H %d/8, CTX=L→CRX=L %d/8\n", highFollow, lowFollow);
    if (highFollow >= 7 && lowFollow >= 7) {
        Serial.println("  → CRXがCTXに追従。SN65HVD230は給電・動作とも正常（原因はビットタイミング/終端側）");
    } else if (highFollow >= 7 && lowFollow == 0) {
        Serial.println("  → CRXがHigh固着＝バスをdominantに引けていない。モジュールの3.3V給電、CTX(GPIO14)の結線、Rsピンのプルダウンを確認");
    } else if (highFollow == 0 && lowFollow >= 7) {
        Serial.println("  → CRXがLow固着＝バスがdominant固着。CANH-CANL間の短絡、終端抵抗、モジュール故障を確認");
    } else {
        Serial.println("  → CRXが不定。CRX(GPIO13)の結線とモジュールのGND共通接続を確認");
    }

    dji::begin(CAN_TX_PIN, CAN_RX_PIN);  // 通常モードへ戻す
}

// ESP32↔SN65HVD230間の配線とトランシーバ動作を、Roninを介さず単体で確認する。
// NO_ACKモード＋自己受信で「TX→トランシーバ→バス→RX」を一周させ、戻ってくれば
// ESP32側は健全と分かり、疑いをRSAコネクタ／Ronin側に絞れる。
static void canLoopbackTest() {
    twai_stop();
    twai_driver_uninstall();

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
        Serial.println("ループバック試験: ドライバ再構成に失敗");
        return;
    }

    twai_message_t tx = {};
    tx.identifier = 0x7FF;
    tx.self = 1;  // 自分で受信する
    tx.data_length_code = 1;
    tx.data[0] = 0xA5;

    bool ok = false;
    if (twai_transmit(&tx, pdMS_TO_TICKS(100)) == ESP_OK) {
        twai_message_t rx;
        uint32_t deadline = millis() + 300;
        while (millis() < deadline) {
            if (twai_receive(&rx, pdMS_TO_TICKS(10)) == ESP_OK &&
                rx.identifier == 0x7FF && rx.data[0] == 0xA5) { ok = true; break; }
        }
    }

    if (ok) {
        Serial.println("ループバック試験OK — ESP32とSN65HVD230間の配線・トランシーバは正常");
        Serial.println("  → 疑いはRSAポート側（コネクタ接触/CANH-CANL極性/AD_COMプルダウン/ジンバル電源）");
    } else {
        Serial.println("ループバック試験NG — ESP32〜SN65HVD230間で信号が一周しない");
        twai_stop();
        twai_driver_uninstall();
        canInternalLoopbackTest();  // まずESP32側の潔白を確かめる
        canPinLevelTest();          // 次にどの層で切れているかをDCレベルで見る
        return;
    }

    twai_stop();
    twai_driver_uninstall();
    dji::begin(CAN_TX_PIN, CAN_RX_PIN);  // 通常モードへ戻す
}

// 起動時にRoninへ角度を1回要求し、CAN配線（RSAポート結線）の生死をシリアルに出す
static void canSelfTest() {
    dji::requestAngles();
    dji::Angles a;
    uint32_t deadline = millis() + 1000;
    while (millis() < deadline) {
        if (dji::pollAngles(a)) {
            Serial.printf("CAN自己診断OK — Ronin応答 yaw=%.1f pitch=%.1f roll=%.1f (deg)\n",
                          a.yaw / 10.0, a.pitch / 10.0, a.roll / 10.0);
            return;
        }
        delay(10);
    }
    // TXがACKされたかでバス上に相手がいるか切り分ける。
    // 誰もACKしないとtx_error_counterが上がりerror-passiveへ落ちる＝結線側の疑い。
    // カウンタが0のままならバスは生きていてRoninが黙っている＝プロトコル側の疑い。
    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
        const char *state = st.state == TWAI_STATE_RUNNING     ? "RUNNING"
                          : st.state == TWAI_STATE_BUS_OFF     ? "BUS_OFF"
                          : st.state == TWAI_STATE_RECOVERING  ? "RECOVERING"
                                                               : "STOPPED";
        Serial.printf("CAN自己診断NG — Roninから応答なし [state=%s tx_err=%u rx_err=%u tx_failed=%u rx=%u]\n",
                      state, st.tx_error_counter, st.rx_error_counter,
                      st.tx_failed_count, st.msgs_to_rx);
        if (st.tx_error_counter > 0 || st.state != TWAI_STATE_RUNNING) {
            Serial.println("  → TXがACKされていない＝バス上に相手がいない");
            canLoopbackTest();  // ESP32側か配線の先かをさらに切り分ける
        } else {
            Serial.println("  → バスは生きている。Ronin側が応答していない（コマンドIDやCRCなどプロトコル側を確認）");
        }
    } else {
        Serial.println("CAN自己診断NG — Roninから応答なし（TWAI状態取得も失敗）");
    }
}

void setup() {
    Serial.begin(115200);

    if (!dji::begin(CAN_TX_PIN, CAN_RX_PIN)) {
        Serial.println("CAN(TWAI)初期化失敗 — 配線とピン設定を確認");
    } else {
        Serial.println("CAN 1Mbps 起動OK");
        canSelfTest();
    }

#ifdef NET_ETH
    // 有線LAN（リンク確立を待ってから固定IPを設定）
    ETH.begin();
#if USE_STATIC_IP
    ETH.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK));
#endif
    Serial.print("Ethernetリンク待ち");
    uint32_t linkDeadline = millis() + LINK_WAIT_MS;
    while (!ETH.linkUp() && millis() < linkDeadline) { delay(300); Serial.print("."); }
#if !USE_STATIC_IP
    while (ETH.localIP() == IPAddress() && millis() < linkDeadline) { delay(300); Serial.print("."); }
#endif
    if (ETH.linkUp()) {
        Serial.printf("\nIP: %s (Ethernet)\n", ETH.localIP().toString().c_str());
    } else {
        Serial.println("\nEthernetリンクなし — LANケーブル未接続のまま続行（接続され次第復帰）");
    }
#else
    // WiFi（検証・予備用）
#if USE_STATIC_IP
    WiFi.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK));
#endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("WiFi接続中");
    uint32_t wifiDeadline = millis() + LINK_WAIT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) { delay(300); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nIP: %s (WiFi)\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi未接続のまま続行（接続され次第復帰）");
    }
#endif

    udp.begin(UDP_PORT);
}

void handlePacket(const char *buf) {
    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) return;
    const char *t = doc["t"];
    if (!t) return;

    if (strcmp(t, "spd") == 0) {
        spdYaw = doc["y"] | 0;
        spdPitch = doc["p"] | 0;
        spdRoll = doc["r"] | 0;
        moving = (spdYaw != 0 || spdPitch != 0 || spdRoll != 0);
        lastSpdCmd = millis();
        dji::speedControl(spdYaw, spdRoll, spdPitch);
        lastSpdSent = millis();
    } else if (strcmp(t, "pos") == 0) {
        moving = false;  // 位置移動中は速度再送を止める
        dji::positionControl(doc["y"] | 0, doc["r"] | 0, doc["p"] | 0,
                             doc["ms"] | 2000);
    } else if (strcmp(t, "gp") == 0) {
        dji::requestAngles();
    }
}

void loop() {
    // UDP受信
    int len = udp.parsePacket();
    if (len > 0) {
        static char buf[256];
        int n = udp.read(buf, sizeof(buf) - 1);
        buf[max(n, 0)] = '\0';
        lastClient = udp.remoteIP();
        lastClientPort = udp.remotePort();
        handlePacket(buf);
    }

    uint32_t now = millis();

    // ウォッチドッグ: 指令が途絶えたら停止（暴走防止の最重要処理）
    if (moving && now - lastSpdCmd > SPEED_WATCHDOG_MS) {
        spdYaw = spdPitch = spdRoll = 0;
        moving = false;
        dji::speedControl(0, 0, 0);
    }

    // 動作中は50msごとに速度指令を再送（ジンバル側のタイムアウト対策）
    if (moving && now - lastSpdSent >= 50) {
        dji::speedControl(spdYaw, spdRoll, spdPitch);
        lastSpdSent = now;
    }

    // Ronin未応答の間は1秒ごとに角度を要求し直し、バスオフからも復帰させる
    if (!roninSeen && now - lastProbe >= 1000) {
        lastProbe = now;
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if (st.state == TWAI_STATE_BUS_OFF) {
                // twai_initiate_recovery()はドライバ内部のtx_msg_countが
                // 負値になるアサート落ち（再起動ループ）を誘発したため使わない。
                // 代わりにドライバごと作り直して復帰する（診断関数群と同じ手法）。
                Serial.println("CANバスオフ → ドライバ再初期化で復帰");
                twai_stop();
                twai_driver_uninstall();
                dji::begin(CAN_TX_PIN, CAN_RX_PIN);
            } else if (st.state == TWAI_STATE_STOPPED) {
                twai_start();
            } else if (st.state == TWAI_STATE_RUNNING) {
                // RECOVERING中はtwai_transmit()がドライバ内部アサートを踏んで
                // 再起動ループに陥るため、完全にRUNNINGへ戻るまで送信しない
                dji::requestAngles();
            }
            // カウンタが動いた時だけ出す（毎秒垂れ流さない）
            if ((int)st.tx_error_counter != lastReportedTxErr) {
                Serial.printf("CAN待機中 [state=%d tx_err=%u rx=%u] — 配線を変えたらここが動きます\n",
                              (int)st.state, st.tx_error_counter, st.msgs_to_rx);
                lastReportedTxErr = (int)st.tx_error_counter;
            }
        }
    }

    // CAN受信 → 角度応答をUDPで返す
    dji::Angles ang;
    if (dji::pollAngles(ang)) {
        if (!roninSeen) {
            roninSeen = true;
            Serial.printf("★Ronin応答を検出 yaw=%.1f pitch=%.1f roll=%.1f (deg)\n",
                          ang.yaw / 10.0, ang.pitch / 10.0, ang.roll / 10.0);
        }
        if (lastClientPort) {
            char out[96];
            snprintf(out, sizeof(out), "{\"t\":\"ang\",\"y\":%d,\"p\":%d,\"r\":%d}",
                     ang.yaw, ang.pitch, ang.roll);
            udp.beginPacket(lastClient, lastClientPort);
            udp.write((const uint8_t *)out, strlen(out));
            udp.endPacket();
        }
    }

    delay(2);
}
