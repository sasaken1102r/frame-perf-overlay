#include "widget_text.h"

#include <cstdio>

namespace frame_perf_overlay {

namespace {

const WidgetText kJapanese = {
    WidgetLanguage::Ja,
    "",  // 日本語は理由の文だけで分かるので前に何も付けない
    // Titles
    "フレーム",
    "CPU",
    "GPU・メモリ",
    "電力",
    "電池",
    "温度",
    "通信",
    "その他",
    // Frame
    "計測待ち",
    "SteamVR 未接続",
    "再投影 ",
    "落ち ",
    // CPU
    "（最大コア ",
    "）",
    // GPU & RAM
    "負荷",
    "クロック ",
    "メモリ ",
    // Power
    "各ch計 ",
    // Battery
    "実際の残量の %。Steam の表示はこれより高め",
    "電池へ",
    "電池から",
    "充電中",
    "放電中",
    "満充電",
    "給電中",
    "待機",
    // Temp
    "℃ CPU",
    "℃",
    "ファン ",
    "熱で制限中 ",
    "CPU・GPU",
    "DDR",
    "電池",
    "画面",
    "排気",
    "放熱",
    // Network
    "直通",
    "Wi-Fi",
    "未接続",
    "リンク ",
    // General
    "コントローラー",
    "左手 ",
    "右手 ",
    "現在時刻",
    // 小さく表示しないときの行と、プロセスの一覧
    "CPU 上位",
    "メモリ上位",
    " ms  ·  最大 ",
    "本来 ",
    "未計測",
    "容量",
    "設計",
    "満充電まで",
    "空になるまで",
    "電池の状態",
    "サイクル",
    "充電モード",
    "設定電流",
    "USB",
    "未接続",
    "PD 出力",
    "実際の入力",
    "電圧の落ち",
    " 大きめ",
    "NPU",
    "カメラ",
    "映像",
    "Wi-Fi",
    "ストレージ",
    "nRF",
    "端子",
    "1 コアに対する %",
    "実メモリ",
    "IP ",
    "起動から",
    "アプリ",
    "なし（SteamVR ホーム）",
    "パネル負荷",
    "hide_rules.json にエラー。前のルールを使っています",
};

const WidgetText kEnglish = {
    WidgetLanguage::En,
    "N/A: ",
    // Titles
    "Frame",
    "CPU",
    "GPU & RAM",
    "Power",
    "Battery",
    "Temp",
    "Network",
    "General",
    // Frame
    "Measuring",
    "No SteamVR",
    "Reproj ",
    "Dropped ",
    // CPU
    " (max core ",
    ")",
    // GPU & RAM
    "load",
    "Clock ",
    "RAM ",
    // Power
    "Rails ",
    // Battery
    "Real gauge %. Steam shows a higher number",
    "Into battery",
    "From battery",
    "Charging",
    "On battery",
    "Full",
    "Plugged",
    "Idle",
    // Temp
    "°C CPU",
    "°C",
    "Fan ",
    "Thermal limit ",
    "CPU+GPU",
    "DDR",
    "Battery",
    "Display",
    "Exhaust",
    "Heatsink",
    // Network
    "Dongle",
    "Wi-Fi",
    "Not connected",
    "Link ",
    // General
    "Controllers",
    "L ",
    "R ",
    "Time",
    // The full (non-compact) rows and the process lists
    "CPU items",
    "RAM items",
    " ms  ·  max ",
    "capped from ",
    "Unmeasured",
    "Charge",
    "design",
    "To full",
    "To empty",
    "Health",
    "cycles",
    "Charger",
    "Set current",
    "USB",
    "Unplugged",
    "PD offered",
    "Arriving",
    "Sag",
    " high",
    "NPU",
    "Camera",
    "Video",
    "Wi-Fi",
    "Flash",
    "nRF",
    "Connector",
    "CPU % of one core",
    "RSS",
    "IP ",
    "Uptime",
    "App",
    "none (SteamVR home)",
    "HUD cost",
    "hide_rules.json has an error, using the last rules",
};

}  // namespace

const WidgetText& widgetText(WidgetLanguage language) {
    return language == WidgetLanguage::En ? kEnglish : kJapanese;
}

const char* reasonLabel(const WidgetText& t, Reason reason) {
    if (t.language == WidgetLanguage::En) return reasonText(reason);
    switch (reason) {
        case Reason::None: return "正常";
        case Reason::NotSampledYet: return "読み取り待ち";
        case Reason::SensorMissing: return "センサーなし";
        case Reason::ReadFailed: return "読み取り失敗";
        case Reason::NoSteamVr: return "SteamVR 未接続";
        case Reason::NoFrames: return "フレーム待ち";
        case Reason::NotConnected: return "未接続";
        case Reason::Paused: return "一時停止中（だれも見ていない）";
        case Reason::InvalidReading: return "意味のない値";
    }
    return "不明";
}

std::string throttleBadgeText(const WidgetText& t, double hz, int throttled) {
    char hzText[16];
    std::snprintf(hzText, sizeof(hzText), "%.0f", hz);
    const std::string fraction = "1/" + std::to_string(throttled + 1);
    if (t.language == WidgetLanguage::En) return "Throttled to " + fraction + " of " + hzText + "Hz (reproj)";
    return std::string(hzText) + "Hz の" + (throttled == 1 ? std::string("半分") : fraction) + "に制限（再投影）";
}

}  // namespace frame_perf_overlay
