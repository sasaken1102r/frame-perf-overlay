// 設定（~/.config/frame-perf-overlay/config.json）の読み込みと変更の監視。
#pragma once

#include "i18n.h"
#include "layout.h"      // the widget grid (core/)
#include "thresholds.h"  // in core/, so the widgets share it

#include <ctime>
#include <string>
#include <vector>

/** 性能パネルをどこに固定するか。 */
enum class Attachment {
    Head,        ///< 頭（HMD）
    LeftWrist,   ///< 左手のコントローラー
    RightWrist,  ///< 右手のコントローラー
    Detached,    ///< pinned in the room (standing space, Config::detachedPose). "Room" on the dashboard
};

/** Where a Detached panel sits: a 3×4 standing-space transform, row-major (the layout of vr::HmdMatrix34_t). */
struct DetachedPose {
    bool valid = false;  ///< false until the panel has been placed in the room once
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    bool operator==(const DetachedPose& o) const {
        if (valid != o.valid) return false;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                if (m[r][c] != o.m[r][c]) return false;
            }
        }
        return true;
    }
    bool operator!=(const DetachedPose& o) const { return !(*this == o); }
};

/**
 * 手首（コントローラー）に固定するときの位置と向き。左右の手で別々に持つ。
 * 位置はコントローラーの座標（SteamVR が返すコントローラーの姿勢の軸、m）。設定パネルの微調整のボタンは、これを
 * パネル自身の軸（今の向きのパネルの右・上・面の向き）で動かす。
 * 向きは頭のパネルと同じ約束（パネル自身の軸で yaw → pitch → roll、正で面が右・上・面を見て反時計回り）で、
 * 手首の上に寝かせた向きを pitch −90 として数える（wristTransform のコメントを参照）。
 */
struct WristPose {
    double x = 0.0;       ///< コントローラーの x（m、−0.5〜0.5）
    double y = 0.05;      ///< コントローラーの y（m、−0.5〜0.5）
    double z = 0.08;      ///< コントローラーの z（m、−0.5〜0.5）
    double pitch = -90.0; ///< 上下の傾き（度、−180〜0）。−90 で手首の上に寝かせた向き。増やすと面がパネルの上側へ向く
    double yaw = 0.0;     ///< 左右の向き（度、−180〜180）。正で面がパネルの右側へ向く
    double roll = 0.0;    ///< 画面内の回転（度、−180〜180）。正で面を見て反時計回り

    /**
     * どれか 1 つでも違うか。
     * @param other 比べる相手
     * @return 違えば true
     */
    bool operator!=(const WristPose& other) const {
        return x != other.x || y != other.y || z != other.z || pitch != other.pitch || yaw != other.yaw ||
               roll != other.roll;
    }
};

/**
 * アプリの設定一式。ファイルが無いときはこの既定値で動く。
 */
struct Config {
    bool visible = true;            ///< false でパネルを隠す（読み取りと描画も止める）
    bool updateCheck = true;        ///< false で新しい版の自動確認を止める（手動の［確認］は常に使える）
    Language language = systemLanguage();  ///< 画面の文言の言語（"ja" / "en"。既定は Frame のシステム言語）
    Attachment attachment = Attachment::Head;  ///< パネルの固定先（頭・左手・右手）
    double posX = -0.15;            ///< HMD 基準の位置（m）。右が +x
    double posY = -0.12;            ///< 上が +y
    double posZ = -0.50;            ///< 前が -z
    double yawDeg = 0.0;            ///< 左右の向き（度）。正でパネルの面が右（+x）を向く（-180〜180）
    double pitchDeg = 0.0;          ///< 上下の傾き（度）。正でパネルの面が上（+y）を向く（-90〜90）
    double rollDeg = 0.0;           ///< 画面内の回転（度）。正で面の側から見て反時計回り（-180〜180）
    WristPose leftWrist;            ///< 左手に固定するときの位置と向き
    WristPose rightWrist;           ///< 右手に固定するときの位置と向き
    bool wristFade = true;          ///< 手首のとき、パネルの面が目から外れるほど薄くして消す
    double wristFadeEndDeg = 75.0;  ///< 消えきる角度（度、35〜90）。その 30° 手前から薄くなり始める
    int clockFormat = 24;           ///< 性能パネルの時計（0 = 出さない、12 = 12 時間制、24 = 24 時間制。本体の時刻）
    double widthM = 0.20;           ///< 1 列（512 px）の幅（m）。パネルの幅は列の数 × これ（既定のレイアウトは 2 列）
    double alpha = 0.9;             ///< パネル全体の不透明度（0〜1）
    int updateIntervalMs = 500;     ///< 更新間隔（ms）
    int graphSeconds = 30;          ///< グラフに出す秒数
    std::string fontPath = "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc";
    std::string boldFontPath = "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc";
    Thresholds thresholds;
    frame_perf_overlay::Layout layout = frame_perf_overlay::defaultLayout();  ///< the widget grid ("layout" key)
    bool layoutInFile = false;  ///< the file had a usable "layout" (false: first run, from before the grid, unreadable)
    bool layoutRejected = false;  ///< the file had an unusable "layout" (the next save keeps the file as .bad)
    std::string activeProfile;  ///< the profile last used / saved ("active_profile", e.g. "profile-2"); "" = none
    DetachedPose detachedPose;  ///< where the Detached panel sits ("detached_pose", written only once placed)
    int detachedCurveDeg = 0;   ///< Detached only: the arc the panel wraps, 0 (flat)..90°, 10° steps
                                ///< ("detached_curve_deg", written only when not 0)
};

/**
 * 既定の設定ファイルの場所を返す（$XDG_CONFIG_HOME か ~/.config の下）。
 * @return 設定ファイルのパス
 */
std::string defaultConfigPath();

/**
 * 設定ファイルに書く固定先の名前。
 * @param attachment 固定先
 * @return "head" / "left_wrist" / "right_wrist" / "detached"
 */
const char* attachmentName(Attachment attachment);

/**
 * 今の固定先の手首の位置と向き（固定先が頭のときは左手のもの。呼ぶ側で頭かどうかを見ること）。
 * @param config 設定
 * @return 左手か右手の WristPose
 */
WristPose& selectedWrist(Config& config);

/**
 * 今の固定先の手首の位置と向き（読むだけ）。
 * @param config 設定
 * @return 左手か右手の WristPose
 */
const WristPose& selectedWrist(const Config& config);

/**
 * 設定ファイルを読む。ファイルが無いときは既定値を返して成功扱いにする。
 * 値の範囲外は丸め、知らないキーは warnings に入れる。
 * @param path 設定ファイルのパス
 * @param out 読んだ設定の書き込み先（失敗時は変更しない）
 * @param warnings 気づいたこと（知らないキー、範囲外の値など）の追加先
 * @param error 読めなかったときの理由
 * @return 成功（またはファイルが無い）なら true
 */
bool loadConfig(const std::string& path, Config& out, std::vector<std::string>& warnings, std::string& error);

/**
 * 設定をファイルに保存する（同じフォルダに一時ファイルを書いてから置き換える）。
 * フォルダが無ければ作る。書くのはこの設定ファイルだけ。
 * @param path 設定ファイルのパス
 * @param config 保存する設定
 * @param error 失敗したときの理由
 * @return 保存できたら true
 */
bool saveConfig(const std::string& path, const Config& config, std::string& error);

/**
 * 表示まわり（表示の有無・固定先・位置・向き・両手の位置と向き・傾けると消す・時計・幅・透明度）だけ既定値に戻す。
 * 言語・しきい値・フォントはそのまま。
 * @param config 書き換える設定
 */
void resetDisplaySettings(Config& config);

/**
 * 回転行列を作る。パネル自身の軸で見て yaw → pitch → roll の順に回す（内因的な Y-X-Z の順）。
 * 行列では R = Ry(yaw) · Rx(−pitch) · Rz(roll)（右手系、列ベクトル）。pitch だけ符号を反転しているのは、
 * pitch を「正で面が上を向く」にそろえるため（X 軸まわりの右手の回転は正で面が下を向く）。
 * 回転なしのとき、パネルの表（+z 側）は親の +z を向く。頭のパネルと手首のパネル（wristTransform）で共通に使う。
 * @param yawDeg 左右の向き（度）。正で面が +x へ
 * @param pitchDeg 上下の傾き（度）。正で面が +y へ
 * @param rollDeg 画面内の回転（度）。正で面を見て反時計回り
 * @param r 書き込み先（r[行][列]）
 */
void panelRotation(double yawDeg, double pitchDeg, double rollDeg, double r[3][3]);

/**
 * 頭のパネルの回転行列（HMD 基準）を作る。性能パネルの変換は「位置 × この回転」。
 * 回転なしのとき、パネルの表（+z 側）は頭の方（HMD の +z = 後ろ）を向く。
 * @param config 設定（yawDeg / pitchDeg / rollDeg を使う）
 * @param r 書き込み先（r[行][列]）
 */
void panelRotation(const Config& config, double r[3][3]);

/**
 * 今の位置のまま、パネルの表が頭（HMD 基準の原点）を向くように yaw と pitch を決める。roll は今の値のまま残す
 * （ユーザーが合わせた傾きを消さない。Rz(roll) は (0,0,1) を動かさないので、roll があっても表の向きは同じ）。
 * 表の向き（法線）は R·(0,0,1) = (sin yaw·cos pitch, sin pitch, cos yaw·cos pitch) なので、
 * これが「パネルから原点への向き」−pos / |pos| と一致するように
 * yaw = atan2(−x, −z)、pitch = atan2(−y, √(x² + z²)) とする（0.1° 単位に丸める）。
 * @param config 書き換える設定
 */
void faceHead(Config& config);

/**
 * 今の向きが faceHead() で決まる向きとほぼ同じか（設定パネルの ✓ 用）。
 * @param config 設定
 * @return yaw・pitch の差が 0.25° 未満なら true（roll は問わない）
 */
bool isFacingHead(const Config& config);

/**
 * 設定ファイルの更新時刻を見て、変わっていたら読み直すための小さな監視役。
 */
class ConfigWatcher {
public:
    /**
     * @param path 監視する設定ファイルのパス
     */
    explicit ConfigWatcher(std::string path);

    /**
     * 最初の読み込み。ファイルが無い・壊れているときは既定値を使う。
     * @return 読み込んだ設定
     */
    Config loadInitial();

    /**
     * 更新時刻が変わっていたら読み直す。stat を 1 回呼ぶだけなので毎回呼んでよい。
     * @param config 現在の設定。変わったときだけ書き換える
     * @return 設定を読み直して書き換えたら true
     */
    bool reloadIfChanged(Config& config);

    /**
     * 自分で設定ファイルを書いた直後に呼ぶ。今のファイルの状態を覚えて、次の reloadIfChanged で読み直さないようにする。
     */
    void noteSaved();

    /** @return 監視しているパス */
    const std::string& path() const { return path_; }

    /** @return true if the file exists but the last load couldn't parse it (so don't overwrite it) */
    bool lastLoadFailed() const { return loadFailed_; }

    /**
     * Saves the config (saveConfig) and notes it (noteSaved). If the file on disk couldn't be parsed, or its "layout"
     * was rejected (the default layout replaced it), it's first renamed to `<path>.bad` (replacing an older one), so
     * the user's text survives and the save still persists.
     * Every save of the running app goes through here.
     * @param config what to save
     * @param error why it failed
     * @return true if saved
     */
    bool save(const Config& config, std::string& error);

private:
    std::string path_;
    bool existed_ = false;
    struct timespec mtime_ {};
    off_t size_ = 0;
    mutable bool loadFailed_ = false;
    mutable bool layoutRejected_ = false;  ///< the last load had a "layout" key that couldn't be used

    /**
     * 今のファイルの状態（有無・更新時刻・サイズ）を取る。
     * @param exists ファイルがあるか
     * @param mtime 更新時刻
     * @param size サイズ
     */
    void statFile(bool& exists, struct timespec& mtime, off_t& size) const;

    /**
     * 読み込んでログを出す共通処理。
     * @param config 書き込み先（失敗時は変えない）
     * @return 読めたら true
     */
    bool loadAndLog(Config& config) const;
};
