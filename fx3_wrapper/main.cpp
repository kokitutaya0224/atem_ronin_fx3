// fx3cli — Sony Camera Remote SDK (CrSDK) の JSON Lines ラッパー
//
// bridge/camera_link.py からサブプロセスとして起動され、
// stdin/stdout の1行1JSONでカメラを制御する。
//
//   in:  {"cmd":"connect"}
//        {"cmd":"set_iris_norm","value":0.5}     // 0..1 → 絞りへマップ
//        {"cmd":"set_iso","value":800}
//        {"cmd":"set_shutter","value":50}        // 1/50秒
//        {"cmd":"set_wb_kelvin","value":5600}
//        {"cmd":"set_focus_norm","value":0.3}
//        {"cmd":"set_zoom_speed","value":-1|0|1}
//        {"cmd":"rec","value":true}
//   out: {"ok":true} / {"ok":false,"err":"..."}
//        {"event":"status","connected":true,"rec":false}
//
// ★ビルド前にSony Camera Remote SDKの入手が必要（README.md参照）。
//   CrSDK APIの正確な列挙値はSDKバージョンで異なるため、
//   同梱サンプル app/RemoteCli の CameraDevice.cpp を参照して
//   TODOコメント箇所を合わせ込むこと。

#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>

#if __has_include("CameraRemote_SDK.h")
#define HAS_CRSDK 1
#include "CameraRemote_SDK.h"
#include "CrDeviceProperty.h"
#include "IDeviceCallback.h"
namespace SDK = SCRSDK;
#else
#define HAS_CRSDK 0
#warning "CrSDKヘッダが見つかりません。スタブモードでビルドします"
#endif

static void reply(bool ok, const char *err = nullptr) {
    if (ok) printf("{\"ok\":true}\n");
    else    printf("{\"ok\":false,\"err\":\"%s\"}\n", err ? err : "unknown");
    fflush(stdout);
}

static void status(bool connected, bool rec) {
    printf("{\"event\":\"status\",\"connected\":%s,\"rec\":%s}\n",
           connected ? "true" : "false", rec ? "true" : "false");
    fflush(stdout);
}

// 素朴なJSON値抽出（依存ライブラリなしで済ませる）
static bool jsonStr(const std::string &line, const char *key, std::string &out) {
    auto pos = line.find(std::string("\"") + key + "\"");
    if (pos == std::string::npos) return false;
    pos = line.find(':', pos);
    if (pos == std::string::npos) return false;
    auto q1 = line.find('"', pos);
    auto q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out = line.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

static bool jsonNum(const std::string &line, const char *key, double &out) {
    auto pos = line.find(std::string("\"") + key + "\"");
    if (pos == std::string::npos) return false;
    pos = line.find(':', pos);
    if (pos == std::string::npos) return false;
    out = atof(line.c_str() + pos + 1);
    return true;
}

#if HAS_CRSDK

class Callback : public SDK::IDeviceCallback {
public:
    std::atomic<bool> connected{false};
    void OnConnected(SDK::DeviceConnectionVersioin) override { connected = true; status(true, false); }
    void OnDisconnected(CrInt32u) override { connected = false; status(false, false); }
    void OnPropertyChanged() override {}
    void OnLvPropertyChanged() override {}
    void OnCompleteDownload(CrChar *, CrInt32u) override {}
    void OnWarning(CrInt32u) override {}
    void OnError(CrInt32u) override {}
};

static SDK::CrDeviceHandle g_handle = 0;
static Callback g_cb;

static bool sdkConnect() {
    if (!SDK::Init()) return false;
    SDK::ICrEnumCameraObjectInfo *list = nullptr;
    if (SDK::EnumCameraObjects(&list) != SDK::CrError_None || !list) return false;
    if (list->GetCount() == 0) { list->Release(); return false; }
    // TODO: --serial指定時は複数台からシリアル一致するものを選ぶ
    const SDK::ICrCameraObjectInfo *cam = list->GetCameraObjectInfo(0);
    auto err = SDK::Connect(const_cast<SDK::ICrCameraObjectInfo *>(cam), &g_cb, &g_handle);
    list->Release();
    return err == SDK::CrError_None;
}

static void setProp(CrInt32u code, CrInt64u value) {
    SDK::CrDeviceProperty prop;
    prop.SetCode(code);
    prop.SetCurrentValue(value);
    prop.SetValueType(SDK::CrDataType_UInt32);  // TODO: プロパティごとに要確認
    SDK::SetDeviceProperty(g_handle, &prop);
}

static void handle(const std::string &cmd, double value) {
    if (cmd == "connect") {
        reply(sdkConnect());
    } else if (cmd == "set_iris_norm") {
        // TODO: GetDeviceProperties()でFNumberの可能値リストを取得し、
        //       正規化値0..1を最寄りの絞り値へマップする (RemoteCli参照)
        // setProp(SDK::CrDeviceProperty_FNumber, mapped);
        reply(true);
    } else if (cmd == "set_iso") {
        setProp(SDK::CrDeviceProperty_IsoSensitivity, (CrInt64u)value);
        reply(true);
    } else if (cmd == "set_shutter") {
        // TODO: CrDeviceProperty_ShutterSpeedは分子/分母のパック形式。
        //       1/value秒 → SDKの表現へ変換 (RemoteCli参照)
        reply(true);
    } else if (cmd == "set_wb_kelvin") {
        // WBをカスタム色温度モードにしてケルビン設定
        setProp(SDK::CrDeviceProperty_WhiteBalance, SDK::CrWhiteBalance_ColorTemp);
        setProp(SDK::CrDeviceProperty_ColorTemp, (CrInt64u)value);
        reply(true);
    } else if (cmd == "set_focus_norm") {
        // TODO: CrDeviceProperty_NearFar (MF駆動) or FocusPosition系
        reply(true);
    } else if (cmd == "set_zoom_speed") {
        // TODO: CrDeviceProperty_Zoom_Operation (-8..8, 0=停止)
        setProp(SDK::CrDeviceProperty_Zoom_Operation, (CrInt64u)(int)(value * 4));
        reply(true);
    } else if (cmd == "rec") {
        // 動画REC開始/停止トグルはMovieRecordコマンド (Down→Up)
        SDK::SendCommand(g_handle, SDK::CrCommandId_MovieRecord, SDK::CrCommandParam_Down);
        SDK::SendCommand(g_handle, SDK::CrCommandId_MovieRecord, SDK::CrCommandParam_Up);
        status(g_cb.connected, value > 0.5);
        reply(true);
    } else {
        reply(false, "unknown cmd");
    }
}

#else  // スタブモード: SDKなしでプロトコル動作だけ確認できる

static void handle(const std::string &cmd, double value) {
    fprintf(stderr, "[stub] %s value=%g\n", cmd.c_str(), value);
    if (cmd == "connect") status(true, false);
    if (cmd == "rec") status(true, value > 0.5);
    reply(true);
}

#endif

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    status(false, false);
    std::string line;
    char buf[1024];
    while (fgets(buf, sizeof(buf), stdin)) {
        line = buf;
        std::string cmd;
        double value = 0;
        if (!jsonStr(line, "cmd", cmd)) { reply(false, "no cmd"); continue; }
        jsonNum(line, "value", value);
        handle(cmd, value);
    }
    return 0;
}
