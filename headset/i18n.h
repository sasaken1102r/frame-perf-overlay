// 画面に出す文言の表（日本語・英語）。描画コードの中に文言を書かず、ここから引く。
// ログや --print の出力はここには入れない。性能パネルのウィジェットの文言は core/render/widget_text にある。
#pragma once

#include "widget_text.h"  // the widgets' strings (core/render)

#include <string>

/** 表示の言語。 */
enum class Language { Ja, En };

/**
 * @param language the UI language
 * @return the same language for the widgets' strings (core/render/widget_text.h)
 */
inline frame_perf_overlay::WidgetLanguage widgetLanguage(Language language) {
    return language == Language::Ja ? frame_perf_overlay::WidgetLanguage::Ja : frame_perf_overlay::WidgetLanguage::En;
}

/**
 * Frame のシステム言語（設定ファイルに language が無いときの既定値）。
 * Steam の言語設定（~/.steam/registry.vdf の "language"、読むだけ）が日本語なら日本語、
 * 読めなければ LC_ALL / LC_MESSAGES / LANG を見て、どれでもなければ英語。結果は最初の 1 回だけ調べて覚えておく。
 * @return 言語
 */
Language systemLanguage();

/**
 * パネルと設定パネルの文言一式。
 */
struct UiText {
    // ---- 設定パネル ----
    const char* settingsTitle;
    const char* cardPanel;              ///< 左のカードの見出し（パネル）
    const char* sentenceBreak;          ///< 最下行で 2 つの文をつなぐ区切り（。/ . ）
    const char* panelShown;
    const char* panelHidden;
    const char* rowShow;
    const char* rowLanguage;
    const char* rowPosition;
    const char* rowNudge;
    const char* rowSize;
    const char* rowOpacity;
    const char* rowAutostart;           ///< 自動起動
    const char* autostartNextLaunch;    ///< 次回の SteamVR 起動から
    const char* autostartNotInstalled;  ///< 未インストール
    const char* autostartBusy;          ///< 切り替え中
    const char* autostartFailed;        ///< 切り替えに失敗
    const char* on;
    const char* off;
    const char* bottomLeft;
    const char* bottomCenter;
    const char* bottomRight;
    const char* topLeft;
    const char* topRight;
    const char* moveLeft;
    const char* moveRight;
    const char* moveUp;
    const char* moveDown;
    const char* nearer;
    const char* farther;
    const char* reset;
    const char* quit;           ///< アプリを終了
    const char* quitConfirm;    ///< もう一度押すと終了
    const char* positionNow;    ///< いまの位置（後ろに横・縦・前が続く）
    const char* posX;
    const char* posY;
    const char* posZ;
    const char* cardFacing;     ///< 向きのカードの見出し
    const char* yawLeft;        ///< 面を左へ向ける
    const char* yawRight;       ///< 面を右へ向ける
    const char* pitchUp;        ///< 面を上へ向ける
    const char* pitchDown;      ///< 面を下へ向ける
    const char* rollLeft;       ///< 画面内で左に回す（反時計回り）
    const char* rollRight;      ///< 画面内で右に回す（時計回り）
    const char* faceMe;         ///< 自分に向ける
    const char* faceForward;    ///< 正面向き（回転なし）
    const char* angleStep1;     ///< 向きのボタンを 1° ずつ動かす
    const char* angleStep5;     ///< 向きのボタンを 5° ずつ動かす
    const char* facingNow;      ///< いまの向き（後ろに左右・上下・回転が続く）
    const char* yawName;
    const char* pitchName;
    const char* rollName;
    const char* footer;         ///< 変更はすぐ反映される旨
    // ---- 更新（vendor/frame-updater/strings.md より） ----
    const char* rowUpdateCheck;          ///< 設定ファイルの説明用（「新しい版の確認」）
    const char* updateUpToDateFormat;    ///< `UpToDate`（%s は版）
    const char* updateChecking;          ///< `checking` で、まだ答えがない
    const char* updateAvailableFormat;   ///< `Available`（%s は版）
    const char* updateButton;            ///< 「更新する」を押す（確認へ）
    const char* updateManual;            ///< `installable` が false のときの案内
    const char* updateReleasePage;       ///< リリースページの URL の前
    const char* updateConfirmFormat;     ///< 確認の質問（%s は版）
    const char* updateConfirmHint;       ///< 確認の補足
    const char* updateConfirmYes;        ///< 確認の実行ボタン
    const char* updateConfirmNo;         ///< 確認のやめるボタン
    const char* updateInstallingFormat;  ///< `Installing`（%s は下の手順の文言）
    const char* updateInstalledFormat;   ///< `Installed`（%s は版）
    const char* updateInstallFailed;     ///< `InstallFailed` の見出し（理由が続く）
    const char* updateCheckFailed;       ///< `CheckFailed` の見出し（理由が続く）
    const char* updateCheckNow;          ///< ［確認］ボタン（strings.md のまま。幅は足りている）
    const char* updateRetry;             ///< `InstallFailed` のやり直しボタン
    const char* updateDismiss;           ///< `Installed` / `InstallFailed` を閉じる
    const char* updateLogHint;           ///< 失敗したときの補足（ログの場所）
    // ---- 固定先・手首・時計 ----
    const char* rowAttachment;    ///< 位置のカードの「固定先」
    const char* attachHead;       ///< 固定先: 頭
    const char* attachLeft;       ///< 固定先: 左手（のコントローラー）
    const char* attachRight;      ///< 固定先: 右手（のコントローラー）
    const char* wristPreset;      ///< 手首のときの位置のボタン（手首の標準の位置と向きに戻す）
    const char* wristOffsetFormat;  ///< 手首のときの、いまの位置（%.0f は標準の位置からの距離、cm）
    const char* rowWristFade;     ///< 傾けると消す
    const char* rowFadeAngle;     ///< 消える角度
    const char* fadeRangeFormat;  ///< 消える角度の値（%.0f が 2 つ: 薄くなり始める角度、消えきる角度）
    const char* rowClock;         ///< 時計
    const char* clock12;          ///< 12 時間制のボタン
    const char* clock24;          ///< 24 時間制のボタン
    const char* clockLabel;       ///< 性能パネルの時計の見出し
    // ---- Dashboard pages and the layout editor ----
    const char* pageLayout;          ///< page switch: the layout editor
    const char* pagePlacement;       ///< page switch: attach point, position, size, ...
    const char* editorTitle;         ///< layout card title
    const char* editorHint;          ///< layout card hint
    const char* editorNewColumn;     ///< the empty lane on the right
    const char* editorDropToRemove;  ///< palette card while a widget is dragged over it
    const char* paletteTitle;        ///< palette card title
    const char* paletteNetwork;      ///< palette chip for Network (the card's title names the link)
    const char* selectedNone;        ///< no widget selected
    const char* noSettings;          ///< the selected widget has no settings
    const char* removeWidget;        ///< remove the selected widget
    const char* layoutSaved;         ///< status after an applied edit
    const char* layoutStale;         ///< status after a rejected (stale) edit
    const char* layoutSaveFailed;    ///< status after a failed save
    const char* rowCompact;          ///< the short card (off adds the extra rows)
    // ---- The Profiles page ----
    const char* pageProfiles;        ///< page switch
    const char* cardProfiles;        ///< the list's title
    const char* profileCountFormat;  ///< "%zu / %zu" profiles used
    const char* profileNone;         ///< no profiles yet
    const char* profileActive;       ///< chip on the active profile
    const char* profileUnsaved;      ///< chip: the live layout differs from it
    const char* profileUse;
    const char* profileSave;         ///< overwrite the active profile
    const char* profileRename;
    const char* profileDelete;
    const char* profileNew;
    const char* profileReset;        ///< live layout -> the built-in default
    const char* profileTapAgain;     ///< Use / Reset armed: the live layout's changes would be lost
    const char* profileDeleteAgain;  ///< Delete armed
    const char* profileNotSaved;     ///< no active profile and the live layout isn't saved in any
    const char* profileHint;         ///< what a profile holds
    const char* profileKeyboard;     ///< the keyboard's description when renaming
    // ---- The layout editor's settings for the process lists ----
    const char* rowItems;            ///< CPU / RAM items: rows shown
    const char* rowSumSameName;      ///< one row per process name
    const char* rowHideSystem;       ///< leave out SteamOS / Steam / SteamVR (hide_rules.json)
};

/**
 * 言語の文言の表を返す。
 * @param language 言語
 * @return 文言の表（プログラムの終わりまで有効）
 */
const UiText& uiText(Language language);

/**
 * 更新の手順（`UpdateStatus::step`）を文言にする。知らない手順はそのまま返す。
 * @param language 言語
 * @param step "start" / "download" / "verify" / "extract" / "install"
 * @return 文言
 */
std::string updateStepText(Language language, const std::string& step);

/**
 * 更新のエラーコード（`UpdateStatus::error`）を文言にする。知らないコードは「other」の文言にする。
 * @param language 言語
 * @param error frame-update.sh / update_check.h のエラーコード
 * @return 文言
 */
std::string updateErrorText(Language language, const std::string& error);

/**
 * プロファイルのページの、操作の結果の文言。
 * @param language 言語
 * @param what "saved-as" / "new-failed" / "using" / "use-failed" / "saved-to" / "save-failed" / "keyboard-failed" /
 *             "deleted" / "delete-failed" / "reset" / "reset-failed" / "renamed" / "rename-failed" / "gone"
 * @param name プロファイルの名前（使わない文言もある）
 * @param error 失敗の理由（失敗のときだけ使う）
 * @return 文言
 * @example
 * profileStatusText(Language::Ja, "saved-to", "Racing", "") // "Racing に上書き保存しました"
 */
std::string profileStatusText(Language language, const std::string& what, const std::string& name,
                              const std::string& error);

/**
 * 設定ファイルに書く言語の名前。
 * @param language 言語
 * @return "ja" / "en"
 */
const char* languageCode(Language language);

/**
 * 設定ファイルの言語の名前を読む。
 * @param code "ja" / "en"
 * @param language 読めたときの書き込み先
 * @return 知っている名前なら true
 */
bool parseLanguage(const std::string& code, Language& language);
