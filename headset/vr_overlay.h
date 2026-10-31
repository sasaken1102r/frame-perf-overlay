// OpenVR との接続、頭か手首（コントローラー）に固定する性能パネル、ダッシュボードの設定パネル、フレーム時間の取得。
#pragma once

#include "config.h"
#include "history.h"
#include "placement.h"
#include "vk_texture.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

/** ダッシュボードの設定パネルへのポインター操作（座標は画像の左上が原点の px）。 */
struct PointerInput {
    enum class Type { Move, Down, Up, Leave, Scroll };  // Scroll: VREvent_ScrollSmooth（レイアウトの編集の列）
    Type type;
    double x = 0.0;
    double y = 0.0;
    double scrollDy = 0.0;  ///< Scroll の ydelta
};

/**
 * 設定パネル（ダッシュボードのこのアプリのタブ）が見えているかの材料。
 */
struct SettingsVisibility {
    bool dashboardVisible = false;  ///< IVROverlay::IsDashboardVisible()（ダッシュボード全体が開いている）
    bool activeTab = false;         ///< IVROverlay::IsActiveDashboardOverlay(設定パネル)（このアプリのタブが選ばれている）
    bool overlayVisible = false;    ///< IVROverlay::IsOverlayVisible(設定パネル)

    /**
     * 実際に見えているか。ダッシュボード全体が開いているだけでは見えている扱いにしない。
     * @return 3 つとも true なら true
     */
    bool visible() const { return dashboardVisible && activeTab && overlayVisible; }
};

/** pollEvents() の結果。 */
struct VrEvents {
    bool quit = false;                  ///< SteamVR から終了を求められた（VREvent_Quit。SteamVR 自体が終わる）
    bool closeRequested = false;        ///< ダッシュボードのアイコンの「閉じる」が押された（VREvent_OverlayClosed）
    std::vector<PointerInput> pointer;  ///< 設定パネルへの操作（届いた順）
    bool keyboardDone = false;          ///< Done on the keyboard showKeyboard() opened; keyboardText has the text
    std::string keyboardText;
    bool keyboardClosed = false;        ///< the keyboard closed without Done
    bool sceneAppChanged = false;       ///< VREvent_SceneApplicationChanged arrived: re-query the scene app

    /** イベントを捨て、次の読み取りに配列の容量を再利用する。 */
    void clear() {
        quit = closeRequested = keyboardDone = keyboardClosed = sceneAppChanged = false;
        pointer.clear();
        keyboardText.clear();
    }
};

/** The app drawing the scene (IVRApplications). */
struct SceneApp {
    uint32_t pid = 0;  ///< 0 = none (the compositor is showing)
    std::string key;   ///< GetApplicationKeyByProcessId, or "process.<comm>" for a process with no app key
    std::string name;  ///< VRApplicationProperty_Name_String, may be empty
};

/**
 * OpenVR のオーバーレイアプリとしての接続をまとめたクラス。
 * SteamVR の設定は変えない（オーバーレイを作って出すだけ）。
 * 画像は SetOverlayRaw ではなく Vulkan の画像を SetOverlayTexture で渡す。
 */
class VrOverlay {
public:
    /** connect() の結果。 */
    enum class ConnectResult {
        Ok,          ///< つながった
        NotRunning,  ///< SteamVR が起動していない（待って再試行する）
        Error,       ///< それ以外の失敗
    };

    VrOverlay();
    ~VrOverlay();
    VrOverlay(const VrOverlay&) = delete;
    VrOverlay& operator=(const VrOverlay&) = delete;

    /**
     * SteamVR が起動していればオーバーレイアプリとしてつなぎ、性能パネル・Vulkan・設定パネルを用意する。
     * 起動していないときに SteamVR を立ち上げてしまわないよう、先に Background 型で有無を確かめる。
     * @param panelWidth 性能パネルの画像の幅（px）
     * @param panelHeight 性能パネルの画像の高さ（px）
     * @param settingsWidth 設定パネルの画像の幅（px）
     * @param settingsHeight 設定パネルの画像の高さ（px）
     * @param message 失敗したときの理由
     * @return 接続結果
     */
    ConnectResult connect(int panelWidth, int panelHeight, int settingsWidth, int settingsHeight,
                          std::string& message);

    /**
     * 診断用: Background 型でだけつなぐ（オーバーレイも Vulkan も作らないので、常駐しているインスタンスと
     * ぶつからない。SteamVR が起動していなければ起動させずに失敗する）。
     * @param message 失敗したときの理由
     * @return つながったら true
     */
    bool connectReadOnly(std::string& message);

    /**
     * 安全な順番で片付ける: 隠す → テクスチャを外す → オーバーレイを消す → コンポジタの数フレーム分待つ
     * → VR_Shutdown → Vulkan の画像とデバイスを壊す。各 API の戻り値はログに出す。
     */
    void shutdown();

    /**
     * 固定先・位置・向き・幅・透明度・表示の有無を性能パネルに反映する。
     * @param config 反映する設定
     */
    void applyConfig(const Config& config);

    /**
     * 性能パネルの置き場所と見え方を反映する（性能パネルの描画とは別の周期で呼ぶ）。
     * 手首のときはコントローラーと頭の姿勢を読んで、面が目から外れる角度に合わせて薄くし、トラッキングが外れたらすぐ隠す。
     * 変わったところだけ OpenVR に渡し、断られた操作は次の呼び出しでやり直す。次に呼ぶ時刻は nextPlacementAt() で分かる。
     * @param config 今の設定
     * @param now 今の時刻（秒、単調増加）
     */
    void updatePlacement(const Config& config, double now);

    /**
     * 次に updatePlacement() を呼ぶ時刻。手首のときはフェードの途中なら 1/30 秒後、見えきっている・消えきっているなら
     * 0.1 秒後。頭のときや隠しているときは、やり直しが要るときだけ（それ以外は無限大）。
     * @return 時刻（秒、単調増加）
     */
    double nextPlacementAt() const { return nextPlacementAt_; }

    /**
     * Whether the panel is really shown: connected, and OpenVR took the last ShowOverlay. False while it's hidden by
     * the setting, by the wrist fade or lost tracking, or at alpha 0 (updatePlacement keeps checking, so it comes
     * back by itself).
     */
    bool panelShown() const { return connected_ && panelHandle_ != 0 && presentation_.shown(); }

    /**
     * --verbose のとき true にする。applyConfig のたびに、少し後の pollEvents で
     * パネルの向きの確かめ（logFacingCheck）をログに出す。
     * @param verbose 出すなら true
     */
    void setVerbose(bool verbose) { verbose_ = verbose; }

    /**
     * たまったイベントを処理する。SteamVR の終了（VREvent_Quit）には AcknowledgeQuit_Exiting で応える。
     * @param includeSystem false なら設定パネルのイベント（マウス・閉じる）だけ見る（設定パネルが見えている間の
     *                      細かい周期用）。true なら SteamVR 全体と性能パネルのイベントも見る
     * @param result 終了要求と設定パネルへの操作。前の結果は消し、配列の容量は再利用する
     */
    void pollEvents(VrEvents& result, bool includeSystem = true);

    /**
     * 接続時に見つけた vrserver のプロセスがまだ生きているか。
     * @return 生きている（または確かめられない）なら true
     */
    bool steamVrAlive() const;

    /**
     * 設定パネル（ダッシュボードのこのアプリのタブ）が今見えているかの材料を読む（IPC 3 回）。
     * @return 材料。visible() で判定する
     */
    SettingsVisibility settingsVisibility() const;

    /**
     * SteamVR のダッシュボードが開いているか（--verbose の診断用）。
     * @return 開いていれば true
     */
    bool dashboardOpen() const;

    /**
     * HMD activity level (IVRSystem::GetTrackedDeviceActivityLevel for the HMD, one IPC). Off-head reads 3
     * (k_EDeviceActivityLevel_Standby): nobody sees the panel then, so its sources pause.
     * @return the EDeviceActivityLevel value, or -1 (unknown) when not connected
     */
    int hmdActivityLevel() const;

    /** @return 設定パネル（ダッシュボード）を作れていれば true */
    bool hasSettings() const { return dashboardHandle_ != 0; }

    /**
     * ダッシュボードのサムネイル画像を送る（接続直後に 1 回）。
     * @param rgba 非乗算済み RGBA
     * @param size 一辺の px
     * @return 送れたら true
     */
    bool submitThumbnail(const uint8_t* rgba, int size);

    /**
     * 前回の呼び出しからのフレーム時間を集計する。
     * @return 集計結果（取れなければ valid = false）
     */
    FrameStats readFrameStats();

    /**
     * 左右のコントローラーの電池を読む（呼ぶのは 10 秒おき程度で十分）。
     * @return 左右の電池。つながっていない側は present = false
     */
    ControllerStatus readControllers();

    /**
     * Reads the current scene app (GetCurrentSceneProcessId, then GetApplicationKeyByProcessId and the name).
     * Works in the read-only connection too.
     * @param app the result
     * @return false when not connected
     */
    bool readSceneApp(SceneApp& app) const;

    /**
     * 診断用: 新しく届いたフレームの生のタイミング（フレーム番号・表示回数・再投影の印など）を
     * 0.5 秒おきに標準出力へ書き出す。
     * @param seconds 書き出す秒数
     * @param stop 途中でやめる印（SIGINT などで 1 になる）
     */
    void dumpFrameTimings(double seconds, const volatile int& stop);

    /**
     * 性能パネルの画像を送る。When the size differs from the current texture (the layout changed), a new texture is
     * made and the old one is kept alive for one more submit, since the compositor may still read it.
     * @param pixels width × height: cairo's premultiplied ARGB32 bytes when panelTakesCairoPixels(), else
     *               non-premultiplied RGBA 8 bit
     * @param width texture width (px)
     * @param height texture height (px)
     * @param columns grid columns: the overlay is width_m × columns wide (width_m is one column's width)
     * @return 送れたら true
     */
    bool submitPanel(const uint8_t* pixels, int width, int height, int columns);

    /**
     * @return true if submitPanel takes cairo's premultiplied ARGB32 bytes as they are (B8G8R8A8 + IsPremultiplied, no
     *         conversion). FRAME_PERF_RGBA_UPLOAD=1 brings back the RGBA conversion (surfaceToRgba)
     */
    bool panelTakesCairoPixels() const { return panelBgra_; }

    /**
     * 設定パネルの画像を送る。
     * @param rgba 非乗算済み RGBA 8bit（connect で指定した大きさ）
     * @return 送れたら true
     */
    bool submitSettings(const uint8_t* rgba);

    /**
     * SteamVR's keyboard for the dashboard tab (profile names). The text comes back in VrEvents::keyboardText when
     * Done is pressed.
     * @param description shown above the keys
     * @param existing the text it starts with
     * @param maxChars longest text allowed
     * @param error why it didn't open
     * @return true if the keyboard is up
     */
    bool showKeyboard(const std::string& description, const std::string& existing, uint32_t maxChars,
                      std::string& error);

private:
    bool connected_ = false;
    uint32_t attachedDevice_ = 0xffffffff;  ///< 変換を渡してある機器（0xffffffff = vr::k_unTrackedDeviceIndexInvalid = まだ）
    bool transformDirty_ = true;            ///< 変換を渡し直す必要がある（設定が変わった・前回断られた）
    vr::HmdMatrix34_t panelTransform_ {};    ///< 設定変更時に作る、機器基準の位置と向き
    WristFadeState wristFade_;              ///< 手首のときの見せる割合（フェード）
    PanelPresentationState presentation_;   ///< OpenVR に反映できた透明度と表示・非表示
    double nextPlacementAt_ = std::numeric_limits<double>::infinity();  ///< 次に updatePlacement を呼ぶ時刻
    uint32_t wristDevice_ = 0xffffffff;     ///< 手首に固定しているコントローラーの番号（覚えておいて、ときどき聞き直す）
    bool wristDeviceLeft_ = true;           ///< wristDevice_ が左手の番号か
    double wristDeviceCheckedAt_ = -1.0;    ///< wristDevice_ を聞いた時刻（まだなら負）
    int panelTextureHeight_ = 0;    ///< 性能パネルのテクスチャの高さ（px。レイアウトで変わる）
    uint64_t panelHandle_ = 0;      ///< vr::VROverlayHandle_t（性能パネル）
    uint64_t dashboardHandle_ = 0;  ///< ダッシュボードの設定パネル
    uint64_t thumbnailHandle_ = 0;  ///< ダッシュボードのサムネイル
    int settingsHeight_ = 0;
    int vrserverPid_ = -1;
    uint32_t lastFrameIndex_ = 0;
    bool haveFrameIndex_ = false;   ///< 前回の readFrameStats でフレームを受け取った
    double displayHz_ = 0.0;
    double hzCheckedAt_ = -1.0;
    bool readOnly_ = false;         ///< connectReadOnly でつないだ（オーバーレイも Vulkan もない）
    bool verbose_ = false;          ///< applyConfig のたびに向きの確かめをログに出す
    double facingCheckAt_ = 0.0;    ///< 向きの確かめをする時刻（0 なら予定なし）
    Config facingCheckConfig_;      ///< 向きの確かめに使う設定（最後に applyConfig したもの）
    std::vector<uint8_t> timingBuffer_;  ///< Compositor_FrameTiming の配列用
    std::string lastPanelError_;
    std::string lastSettingsError_;

    VulkanContext vulkan_;
    std::unique_ptr<OverlayTexture> panelTexture_;         ///< recreated when the layout changes its size
    std::unique_ptr<OverlayTexture> retiredPanelTexture_;  ///< the previous size, destroyed on the next submit
    int panelTextureWidth_ = 0;
    int panelColumns_ = 1;        ///< the overlay is width_m × this wide
    bool panelBgra_ = true;       ///< the panel texture is B8G8R8A8 + premultiplied
    double configWidthM_ = 0.20;  ///< width_m from the last applyConfig
    OverlayTexture settingsTexture_;
    OverlayTexture thumbnailTexture_;

    /**
     * 手首に固定するコントローラーの番号を返す（役割から引いた番号を覚えておき、手を替えたときと 1 秒ごとだけ
     * OpenVR に聞き直す）。
     * @param left 左手なら true
     * @param now 今の時刻（秒、単調増加）
     * @return 番号（見つからなければ vr::k_unTrackedDeviceIndexInvalid）
     */
    uint32_t wristDeviceIndex(bool left, double now);

    /**
     * ダッシュボードの設定パネルとサムネイルを作る。失敗しても性能パネルは動かす。
     * @param width 設定パネルの幅（px）
     * @param height 設定パネルの高さ（px）
     */
    void createDashboard(int width, int height);

    /**
     * 診断用（--verbose）: 今の HMD の姿勢から、頭の中心 → パネル上の 3 点（中心・パネル自身の +x 方向に 2cm・
     * +y 方向に 2cm）へ光線を飛ばし、SteamVR が計算した当たり（ComputeOverlayIntersection の UV・法線・距離）を
     * ログに出す。法線は HMD 基準に直して、こちらで計算した表の向き R·(0,0,1) と、頭への向き −pos / |pos| との
     * 内積も出す（1 に近ければ表が頭を向いている）。
     * @param config 反映した設定
     */
    void logFacingCheck(const Config& config) const;

    /**
     * /proc を一度だけ走査して vrserver の PID を探す。
     * @return 見つかった PID。無ければ -1
     */
    static int findVrserverPid();

    /**
     * HMD のリフレッシュレートを取る（5 秒に 1 回だけ問い合わせる）。
     * @param now 現在時刻（秒）
     * @return Hz。取れなければ 0
     */
    double displayHz(double now);
};
