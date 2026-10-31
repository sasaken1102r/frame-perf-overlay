// 画面に出す文言の表の中身。
#include "i18n.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

/**
 * Steam の言語設定を読む。~/.steam/registry.vdf の最初の "language" の値（"japanese" など）。
 * @return 値。読めなければ空
 */
std::string steamLanguage() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return "";
    const std::string path = std::string(home) + "/.steam/registry.vdf";
    FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) return "";
    char* buffer = nullptr;
    size_t capacity = 0;
    ssize_t count;
    std::string language;
    while ((count = ::getline(&buffer, &capacity, file)) >= 0) {
        // 形式: <タブ>"language"<タブ>"japanese"。長い行も切らずに読む。
        const std::string_view line(buffer, static_cast<size_t>(count));
        constexpr std::string_view key = "\"language\"";
        const size_t at = line.find(key);
        if (at == std::string_view::npos) continue;
        const size_t open = line.find('"', at + key.size());
        const size_t close = open == std::string_view::npos ? open : line.find('"', open + 1);
        if (close != std::string_view::npos) language.assign(line.data() + open + 1, close - open - 1);
        break;
    }
    std::free(buffer);
    std::fclose(file);
    return language;
}

/**
 * ロケールの環境変数（LC_ALL → LC_MESSAGES → LANG の順で最初に空でないもの）が日本語か。
 * @return 日本語なら true
 */
bool localeIsJapanese() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && value[0] != '\0') return value[0] == 'j' && value[1] == 'a';
    }
    return false;
}

/**
 * システム言語を調べる（systemLanguage の本体）。
 * @return 言語
 */
Language detectSystemLanguage() {
    const std::string steam = steamLanguage();
    if (!steam.empty()) return steam == "japanese" ? Language::Ja : Language::En;
    return localeIsJapanese() ? Language::Ja : Language::En;
}

const UiText kJapanese = {
    // 設定パネル
    "Frame Perf 設定", "パネル", "。", "パネル表示中", "パネル非表示",
    "表示", "言語", "位置", "微調整", "大きさ", "透明度",
    "自動起動", "自動起動の切り替えは次回の SteamVR 起動から効きます",
    "自動起動のユニットが入っていないので切り替えられません（README の「自動起動」を見てね）", "自動起動を切り替え中…",
    "自動起動の切り替えに失敗しました（ログを見てね）",
    "オン", "オフ",
    "左下", "中央下", "右下", "左上", "右上",
    "← 左", "右 →", "↑ 上", "↓ 下", "近く", "遠く",
    "既定に戻す", "アプリを終了", "もう一度押すと終了",
    "いまの位置", "横", "縦", "前",
    "向き", "← 左向き", "右向き →", "↑ 上向き", "↓ 下向き", "左に回す", "右に回す", "自分に向ける", "正面向き", "1° ずつ", "5° ずつ",
    "いまの向き", "左右", "上下", "回転",
    "変更はすぐ反映され、設定ファイルに保存されます",
    // 更新
    "新しい版の確認",
    "最新版です（%s）", "新しい版を確かめています…", "新しい版 %s があります",
    "更新する", "ここからは入れられない版です。GitHub から手で更新してね", "リリースページ:",
    "%s に更新しますか？", "ダウンロードして入れ替えます。途中でこの画面が閉じて開き直すことがあります",
    "更新する", "やめる",
    "更新中: %s", "%s を入れました。開き直すと新しい版になります",
    "更新できませんでした（今の版のままです）:", "新しい版を確かめられませんでした:",
    "今すぐ確かめる", "もう一度", "閉じる",
    "くわしくは ~/.cache/frame-perf-overlay/update.log",
    // 固定先・手首・時計
    "固定先", "頭", "左手", "右手", "手首の標準の位置", "標準から %.0f cm",
    "傾けると消す", "消える角度", "%.0f〜%.0f°",
    "時計", "12h", "24h", "現在時刻",
    // ダッシュボードのページ・レイアウトの編集
    "レイアウト",
    "配置",
    "レイアウト",
    "ドラッグで移動、タップで設定を変えられます",
    "新しい列",
    "ここに落とすと外します",
    "ウィジェット",
    "通信",
    "左のウィジェットをタップすると変えられます",
    "設定はありません",
    "外す",
    "保存しました",
    "ほかで変わりました。もう一度やってね",
    "レイアウトを保存できませんでした",
    "小さく表示",
    // プロファイルのページ
    "プロファイル",
    "プロファイル",
    "%zu / %zu",
    "まだありません。「新しく作る」で今のレイアウトを保存します",
    "使用中",
    "未保存の変更あり",
    "使う",
    "上書き保存",
    "名前を変える",
    "削除",
    "新しく作る",
    "既定に戻す",
    "もう一度押すと、レイアウトの変更は消えます",
    "もう一度押すと削除",
    "今のレイアウトはどのプロファイルにも保存されていません",
    "プロファイルに入るのはレイアウト（列・ウィジェットとその設定）だけ。位置やアプリの設定は入りません",
    "プロファイルの名前",
    // The layout editor's widget settings (English until translated)
    "Rows",            // TODO(ja)
    "One row per name",  // TODO(ja)
    "Hide system",     // TODO(ja)
};

const UiText kEnglish = {
    // Settings panel
    "Frame Perf Settings", "Panel", ". ", "Panel shown", "Panel hidden",
    "Show", "Language", "Position", "Nudge", "Size", "Opacity",
    "Autostart", "Autostart changes apply from the next SteamVR start",
    "The autostart unit is not installed, so it can't be switched (see \"Autostart\" in the README)",
    "Switching autostart…", "Failed to switch autostart (see the log)",
    "On", "Off",
    "Bottom L", "Bottom C", "Bottom R", "Top L", "Top R",
    "← Left", "Right →", "↑ Up", "↓ Down", "Closer", "Farther",
    "Reset", "Quit app", "Press again to quit",
    "Now", "x", "y", "z",
    "Facing", "← Left", "Right →", "↑ Up", "↓ Down", "Rotate L", "Rotate R", "Face me", "Face ahead", "By 1°", "By 5°",
    "Now", "yaw", "pitch", "roll",
    "Changes apply instantly and are saved",
    // Updates
    "Check for updates",
    "Up to date (%s)", "Checking for updates…", "Version %s is available",
    "Update", "This version can't be installed from here. Update by hand from GitHub", "Release page:",
    "Update to %s?", "It downloads and installs the new version. This panel may close and reopen meanwhile",
    "Update", "Cancel",
    "Updating: %s", "%s is installed. Reopen to use it",
    "Update failed:", "Couldn't check for updates:",
    "Check now", "Try again", "Close",
    "Details: ~/.cache/frame-perf-overlay/update.log",
    // Attachment, wrist and clock
    "Attach to", "Head", "L hand", "R hand", "Default wrist position", "%.0f cm from default",
    "Fade on tilt", "Fade angle", "%.0f–%.0f°",
    "Clock", "12h", "24h", "Local time",
    // Dashboard pages and the layout editor
    "Layout", "Placement",
    "Layout", "Drag a widget to move it, tap it to change its settings", "New column", "Drop here to remove",
    "Widgets", "Network",
    "Tap a widget on the left to change it", "No settings", "Remove",
    "Saved", "Changed meanwhile, try again", "Couldn't save the layout",
    "Compact",
    // The Profiles page
    "Profiles", "Profiles", "%zu / %zu", "No profiles yet. New profile saves the layout you have now",
    "Active", "unsaved changes",
    "Use", "Save", "Rename", "Delete", "New profile", "Reset to default",
    "Tap again: the layout's changes are lost", "Tap again to delete",
    "The layout you have now isn't saved in a profile",
    "A profile is the layout only: columns, widgets and their settings. Placement and app settings aren't part of it",
    "Profile name",
    // The layout editor's widget settings
    "Rows", "One row per name", "Hide system",
};

}  // namespace

const UiText& uiText(Language language) {
    return language == Language::En ? kEnglish : kJapanese;
}

std::string updateStepText(Language language, const std::string& step) {
    const bool en = language == Language::En;
    if (step == "start") return en ? "Preparing" : "準備中";
    if (step == "download") return en ? "Downloading" : "ダウンロード中";
    if (step == "verify") return en ? "Verifying" : "ファイルを確認中";
    if (step == "extract") return en ? "Unpacking" : "展開中";
    if (step == "install") return en ? "Installing" : "入れ替え中";
    return step;
}

std::string profileStatusText(Language language, const std::string& what, const std::string& name,
                              const std::string& error) {
    const bool en = language == Language::En;
    if (what == "saved-as") return en ? "Saved as " + name : name + " として保存しました";
    if (what == "new-failed") return (en ? "Couldn't save a new profile: " : "新しいプロファイルを保存できませんでした: ") + error;
    if (what == "using") return en ? "Using " + name : name + " を使っています";
    if (what == "use-failed") return en ? "Couldn't use " + name + ": " + error : name + " を使えませんでした: " + error;
    if (what == "saved-to") return en ? "Saved to " + name : name + " に上書き保存しました";
    if (what == "save-failed") return en ? "Couldn't save " + name + ": " + error : name + " を保存できませんでした: " + error;
    if (what == "keyboard-failed") return (en ? "The keyboard didn't open: " : "キーボードを開けませんでした: ") + error;
    if (what == "deleted") return en ? "Deleted " + name : name + " を削除しました";
    if (what == "delete-failed") return en ? "Couldn't delete " + name + ": " + error : name + " を削除できませんでした: " + error;
    if (what == "reset") return en ? "Layout reset to the default" : "レイアウトを既定に戻しました";
    if (what == "reset-failed") return (en ? "Couldn't reset: " : "既定に戻せませんでした: ") + error;
    if (what == "renamed") return en ? "Renamed to " + name : "名前を " + name + " に変えました";
    if (what == "rename-failed") return (en ? "Couldn't rename: " : "名前を変えられませんでした: ") + error;
    return en ? "That profile isn't there any more" : "そのプロファイルはもうありません";
}

std::string updateErrorText(Language language, const std::string& error) {
    const bool en = language == Language::En;
    if (error == "network") return en ? "Can't reach GitHub" : "GitHub につながりません";
    if (error == "rate-limited") {
        return en ? "GitHub's rate limit was hit. Try again in an hour" : "GitHub の回数制限にかかりました。1 時間ほどあとで試してね";
    }
    if (error == "not-found") return en ? "No published release" : "公開されている版がありません";
    if (error == "bad-response") return en ? "Couldn't read GitHub's answer" : "GitHub の返事を読めませんでした";
    if (error == "bad-version") return en ? "Couldn't read the version number" : "版の番号を読めませんでした";
    if (error == "bad-url") return en ? "Stopped: the download led outside GitHub" : "GitHub 以外の場所へ向かったので止めました";
    if (error == "missing-tool") return en ? "A required command (python3) is missing" : "必要なコマンド（python3）がありません";
    if (error == "no-checksums") return en ? "This release has no SHA256SUMS. Update by hand" : "この版には確認用の SHA256SUMS がありません。手で更新してね";
    if (error == "no-asset") return en ? "This release has no file to install" : "この版には入れるファイルがありません";
    if (error == "checksum-mismatch") return en ? "The download is corrupt (checksum mismatch)" : "ダウンロードしたファイルが壊れています";
    if (error == "unsafe-archive") return en ? "Stopped: the archive has unsafe contents" : "ファイルの中身が安全でないので止めました";
    if (error == "no-installer") return en ? "The archive has no install.sh" : "ファイルに install.sh がありません";
    if (error == "install-failed") return en ? "install.sh failed" : "install.sh が失敗しました";
    if (error == "bad-args") return en ? "The saved install options are invalid" : "前回のインストールのオプションを読めません";
    if (error == "busy") return en ? "Another update is running" : "別の更新が動いています";
    if (error == "not-newer") return en ? "Already up to date" : "もう最新版です";
    if (error == "detach-failed") return en ? "Couldn't start the update (systemd-run)" : "更新を始められませんでした（systemd-run）";
    if (error == "interrupted") return en ? "The update was interrupted" : "更新が途中で止まりました";
    if (error == "io") return en ? "Couldn't write files" : "ファイルを書けませんでした";
    if (error == "usage" || error == "script-failed" || error == "spawn-failed") {
        return en ? "The updater didn't run" : "更新の仕組みが動きませんでした";
    }
    return en ? "Something went wrong" : "うまくいきませんでした";
}

const char* languageCode(Language language) {
    return language == Language::En ? "en" : "ja";
}

Language systemLanguage() {
    static const Language cached = detectSystemLanguage();
    return cached;
}

bool parseLanguage(const std::string& code, Language& language) {
    if (code == "ja") {
        language = Language::Ja;
        return true;
    }
    if (code == "en") {
        language = Language::En;
        return true;
    }
    return false;
}
