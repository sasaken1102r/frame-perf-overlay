// 手首への固定・時計・設定パネルのテスト（CTest から動かす。SteamVR は要らない）。
// 計算（位置と向き・フェード・やり直し）、時計の文字列、設定ファイル、設定パネルのボタンの当たり判定と見た目を確かめる。
#include "clock.h"
#include "config.h"
#include "draw.h"
#include "grid_renderer.h"
#include "placement.h"
#include "settings_panel.h"
#include "theme.h"

#include <cairo.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/** 条件が偽なら、式と行番号を持った例外を投げる。 */
#define CHECK(expr)                                                                                  \
    do {                                                                                             \
        if (!(expr)) throw std::runtime_error(std::string(#expr) + " (line " + std::to_string(__LINE__) + ")"); \
    } while (false)

/**
 * 2 つの数がほぼ同じか確かめる。
 * @param actual 実際の値
 * @param expected 期待する値
 * @param epsilon 許す差
 * @param line 呼んだ行（失敗したときの表示用）
 */
void nearAt(double actual, double expected, double epsilon, int line) {
    if (std::fabs(actual - expected) < epsilon) return;
    char text[160];
    std::snprintf(text, sizeof(text), "near: %.6f != %.6f (line %d)", actual, expected, line);
    throw std::runtime_error(text);
}
#define NEAR(actual, expected) nearAt((actual), (expected), 1e-4, __LINE__)

/** 3 次元のベクトル。 */
struct Vec3 {
    double x, y, z;
};

/**
 * 変換の列（0 = パネルの右、1 = 上、2 = 面の向き）を取り出す。
 * @param m 変換
 * @param col 列
 * @return ベクトル
 */
Vec3 column(const vr::HmdMatrix34_t& m, int col) {
    return {m.m[0][col], m.m[1][col], m.m[2][col]};
}

/**
 * 内積。
 * @param a ベクトル
 * @param b ベクトル
 * @return a·b
 */
double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * 頭のパネルと同じ回転（panelRotation）と位置から、テスト用の変換を作る。
 * @param yaw 左右（度）
 * @param pitch 上下（度）
 * @param roll 回転（度）
 * @param x 位置 x（m）
 * @param y 位置 y（m）
 * @param z 位置 z（m）
 * @return 変換
 */
vr::HmdMatrix34_t makeTransform(double yaw, double pitch, double roll, double x, double y, double z) {
    double r[3][3];
    panelRotation(yaw, pitch, roll, r);
    vr::HmdMatrix34_t m {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) m.m[row][col] = static_cast<float>(r[row][col]);
    }
    m.m[0][3] = static_cast<float>(x);
    m.m[1][3] = static_cast<float>(y);
    m.m[2][3] = static_cast<float>(z);
    return m;
}

/**
 * 今の固定先のパネルの回転（頭なら panelRotation、手首なら wristTransform）を変換で返す。
 * @param config 設定
 * @return 変換（位置は 0）
 */
vr::HmdMatrix34_t currentRotation(const Config& config) {
    if (config.attachment == Attachment::Head) return makeTransform(config.yawDeg, config.pitchDeg, config.rollDeg, 0, 0, 0);
    WristPose pose = selectedWrist(config);
    pose.x = pose.y = pose.z = 0;
    return wristTransform(pose);
}

/**
 * 向きのボタンを 1 回押したとき、パネル自身の軸で見て決まった向きに回っているか確かめる（頭でも手首でも同じ約束）。
 * ↑ 上向き: 面がパネルの上側へ、→ 右向き: 面がパネルの右側へ、⟲ 左に回す: 上側がパネルの左側へ（面を見て反時計回り）。
 * 左右（yaw）は親の縦軸（頭: HMD の上、手首: 寝かせた向きの上側）まわりなので、上下の傾きが 0（手首は −90）のときに見る。
 * @param config 設定（固定先と今の向き。roll は 0 にしておく）
 * @param line 呼んだ行
 */
void checkRotationConvention(Config config, int line) {
    const double step = 5.0;
    const double c = std::cos(step * M_PI / 180.0);
    const double s = std::sin(step * M_PI / 180.0);
    const auto before = currentRotation(config);
    const Vec3 right = column(before, 0), up = column(before, 1), normal = column(before, 2);

    Config pitched = config;
    if (!applySettingsAction(SettingsAction::PitchUp, pitched, step)) throw std::runtime_error("PitchUp (line " + std::to_string(line) + ")");
    const Vec3 n1 = column(currentRotation(pitched), 2);
    nearAt(dot(n1, normal), c, 1e-4, line);
    nearAt(dot(n1, up), s, 1e-4, line);

    Config yawed = config;
    if (!applySettingsAction(SettingsAction::YawRight, yawed, step)) throw std::runtime_error("YawRight (line " + std::to_string(line) + ")");
    const Vec3 n2 = column(currentRotation(yawed), 2);
    nearAt(dot(n2, normal), c, 1e-4, line);
    nearAt(dot(n2, right), s, 1e-4, line);

    Config rolled = config;
    if (!applySettingsAction(SettingsAction::RollLeft, rolled, step)) throw std::runtime_error("RollLeft (line " + std::to_string(line) + ")");
    const auto after = currentRotation(rolled);
    nearAt(dot(column(after, 2), normal), 1.0, 1e-4, line);  // 面の向きは変わらない
    nearAt(dot(column(after, 1), up), c, 1e-4, line);
    nearAt(dot(column(after, 1), right), -s, 1e-4, line);   // 上側が左へ = 反時計回り
}

/** 位置と向きの計算・フェード・トラッキングの確かめ。 */
void geometry() {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount] {};
    CHECK(!wristTrackingValid(vr::k_unTrackedDeviceIndexInvalid, poses));
    CHECK(!wristTrackingValid(1, poses));
    poses[0].bDeviceIsConnected = poses[0].bPoseIsValid = true;
    poses[1].bDeviceIsConnected = poses[1].bPoseIsValid = true;
    CHECK(wristTrackingValid(1, poses));
    poses[1].bPoseIsValid = false;
    CHECK(!wristTrackingValid(1, poses));
    poses[1].bPoseIsValid = true;
    poses[0].bDeviceIsConnected = false;
    CHECK(!wristTrackingValid(1, poses));

    // 面が頭を向いていれば 1、45°（75 − 30）までは 1、60° で半分、75° 以上で 0
    const auto head = makeTransform(0, 0, 0, 0, 0, 0);
    NEAR(wristFacingAlpha(makeTransform(0, 0, 0, 0, 0, -0.5), head, 75), 1);
    NEAR(wristFacingAlpha(makeTransform(45, 0, 0, 0, 0, -0.5), head, 75), 1);
    NEAR(wristFacingAlpha(makeTransform(60, 0, 0, 0, 0, -0.5), head, 75), 0.5);
    NEAR(wristFacingAlpha(makeTransform(90, 0, 0, 0, 0, -0.5), head, 75), 0);
    NEAR(wristFacingAlpha(makeTransform(180, 0, 0, 0, 0, -0.5), head, 75), 0);
    NEAR(wristFacingAlpha(head, head, 75), 0);  // 頭と重なっている
    // 全体を動かしても（コントローラーと頭が一緒に動いても）同じ
    const auto world = makeTransform(41, 17, -22, 2, 3, 4);
    NEAR(wristFacingAlpha(composeTransform(world, makeTransform(60, 0, 0, 0, 0, -0.5)), composeTransform(world, head), 75),
         0.5);

    // 手首の既定（pitch −90）: 面がコントローラーの +y、パネルの上側がコントローラーの −z、右側が +x
    const auto wrist = wristTransform(WristPose());
    NEAR(wrist.m[0][2], 0);
    NEAR(wrist.m[1][2], 1);
    NEAR(wrist.m[2][2], 0);
    NEAR(wrist.m[2][1], -1);
    NEAR(wrist.m[0][0], 1);
    NEAR(wrist.m[0][3], 0);
    NEAR(wrist.m[1][3], 0.05);
    NEAR(wrist.m[2][3], 0.08);
    // 手首の pitch −180 で、面がコントローラーの +z を向き、上側が +y
    WristPose standing;
    standing.pitch = -180;
    const auto stand = wristTransform(standing);
    NEAR(stand.m[2][2], 1);
    NEAR(stand.m[1][1], 1);

    // 向きのボタンの約束は、頭でも手首（既定と、左右に回したところ）でも同じ
    Config c;
    c.attachment = Attachment::Head;
    checkRotationConvention(c, __LINE__);
    c.yawDeg = -30;
    checkRotationConvention(c, __LINE__);
    c.attachment = Attachment::LeftWrist;
    checkRotationConvention(c, __LINE__);
    c.leftWrist.yaw = 25;
    checkRotationConvention(c, __LINE__);
    c.attachment = Attachment::RightWrist;
    checkRotationConvention(c, __LINE__);
    // どんな向きでも、手首の回転は「寝かせた向き（pitch −90）を基準にした、頭のパネルと同じ回転」そのもの:
    // wristTransform(yaw, pitch, roll) = 基準 · panelRotation(yaw, pitch + 90, roll)
    const auto base = wristTransform(WristPose());
    const double samples[][3] = {{-30, -70, 12}, {140, -165, -80}, {5, -10, 179}, {-90, -90, 45}};
    for (const auto& sample : samples) {
        WristPose pose;
        pose.x = pose.y = pose.z = 0;
        pose.yaw = sample[0];
        pose.pitch = sample[1];
        pose.roll = sample[2];
        const auto wristRot = wristTransform(pose);
        const auto headRot = makeTransform(sample[0], sample[1] + 90, sample[2], 0, 0, 0);
        const auto expected = composeTransform(base, headRot);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) NEAR(wristRot.m[row][col], expected.m[row][col]);
        }
    }

    // フェードは時間でなめらかに（呼ぶ間隔によらない）
    double alpha = 0;
    for (int i = 0; i < 60; ++i) {
        const double next = smoothWristAlpha(alpha, 1, 1.0 / 60);
        CHECK(next >= alpha && next <= 1);
        alpha = next;
    }
    NEAR(alpha, smoothWristAlpha(0, 1, 1));
    CHECK(smoothWristAlpha(1, 0, 0.2) < 0.1);
}

/**
 * 手首の微調整は、パネル自身の軸で動く: 「↑ 上」はパネルの上方向、「右 →」は右方向、「近く」は面の向き
 * （見ている人の方）へ 1cm。既定の向きと、いくつかの回転で確かめる。保存するのはコントローラーの座標のまま。
 */
void panelAxisNudges() {
    const double poses[][3] = {{0, -90, 0}, {30, -60, 20}, {-45, -135, -70}, {170, -10, 95}, {0, -180, 0}};
    const struct {
        SettingsAction action;
        int axis;
        double sign;
    } moves[] = {
        {SettingsAction::MoveRight, 0, 1}, {SettingsAction::MoveLeft, 0, -1}, {SettingsAction::MoveUp, 1, 1},
        {SettingsAction::MoveDown, 1, -1}, {SettingsAction::MoveNear, 2, 1},  {SettingsAction::MoveFar, 2, -1},
    };
    for (const auto& angles : poses) {
        for (const auto& move : moves) {
            Config c;
            c.attachment = Attachment::RightWrist;
            c.rightWrist.yaw = angles[0];
            c.rightWrist.pitch = angles[1];
            c.rightWrist.roll = angles[2];
            const WristPose before = c.rightWrist;
            const auto axes = wristTransform(before);
            CHECK(applySettingsAction(move.action, c));
            const double delta[3] = {c.rightWrist.x - before.x, c.rightWrist.y - before.y, c.rightWrist.z - before.z};
            for (int i = 0; i < 3; ++i) {
                // 1mm 単位に丸めるので、各軸 0.6mm までの差は許す
                nearAt(delta[i], move.sign * 0.01 * axes.m[i][move.axis], 0.0006, __LINE__);
            }
            // 向きは変わらない
            NEAR(c.rightWrist.yaw, before.yaw);
            NEAR(c.rightWrist.pitch, before.pitch);
            NEAR(c.rightWrist.roll, before.roll);
        }
    }
    // 既定の向きでは、右 → はコントローラーの +x（前の版と同じ向き）
    Config c;
    c.attachment = Attachment::LeftWrist;
    CHECK(applySettingsAction(SettingsAction::MoveRight, c));
    NEAR(c.leftWrist.x, 0.01);
    NEAR(c.leftWrist.y, 0.05);
    NEAR(c.leftWrist.z, 0.08);
    // ±50cm に収める
    c.leftWrist.x = 0.495;
    CHECK(applySettingsAction(SettingsAction::MoveRight, c));
    NEAR(c.leftWrist.x, 0.5);
    CHECK(!applySettingsAction(SettingsAction::MoveRight, c));
}

/** CPU を抑えるための確かめる間隔と、粗い間隔のあとでもいきなり変わらないこと。 */
void pollScheduling() {
    NEAR(wristPollInterval(1, 1), kWristPollSteadySec);      // 見えきっている
    NEAR(wristPollInterval(0, 0), kWristPollSteadySec);      // 消えきっている
    NEAR(wristPollInterval(0.3, 1), kWristPollFadeSec);      // フェードの途中
    NEAR(wristPollInterval(0.6, 0.6), kWristPollFadeSec);    // 角度がフェードの範囲に入っている
    CHECK(kWristPollFadeSec >= 1.0 / 30.0 - 1e-9);           // 細かいときでも 30Hz まで
    CHECK(kWristPollSteadySec <= 0.1 + 1e-9);                // トラッキングが外れたら 0.1 秒以内に気づく

    WristFadeState fade;
    fade.update(true, true, 1, 1, 10.0);
    for (int i = 1; i <= 60; ++i) fade.update(true, true, 1, 1, 10.0 + i * kWristPollFadeSec);
    CHECK(fade.alpha() > 0.99);
    // 見えきって 0.1 秒おきになったところで急に目標が 0 になっても、1 回で 1/30 秒ぶんしか進めない
    const double t = 10.0 + 60 * kWristPollFadeSec + kWristPollSteadySec;
    const double first = fade.update(true, true, 1, 0, t);
    CHECK(first > 0.6 && first < 0.7);  // 0.1 秒ぶん進めると 0.29 まで落ちてしまう
    // トラッキングが外れたら、フェードを待たずにすぐ 0
    NEAR(fade.update(true, false, 1, 1, t + 0.1), 0);
}

/** 断られた操作のやり直しと、固定先を変えたとき・戻ったときのフェードの始まり。 */
void placementRecovery() {
    WristFadeState fade;
    NEAR(fade.update(false, true, 0, 1, 1), 1);    // 頭はすぐ 1
    NEAR(fade.update(true, true, 1, 1, 61), 0);    // 手首に替えたら 0 から（60 秒前からの時間で飛ばない）
    const double firstStep = fade.update(true, true, 1, 1, 61 + 1.0 / 60);
    CHECK(firstStep > 0 && firstStep < 0.25);
    NEAR(fade.update(true, true, 2, 1, 100), 0);   // しばらくしてから別の手に替えた
    CHECK(fade.update(true, true, 2, 1, 100.1) > 0);
    NEAR(fade.update(true, false, 2, 1, 101), 0);  // トラッキングが外れた・表示オフ
    NEAR(fade.update(true, true, 2, 1, 150), 0);   // 戻ったときも 0 から
    CHECK(fade.update(true, true, 2, 1, 150.1) > 0);
    NEAR(fade.update(false, true, 0, 1, 151), 1);  // 頭はすぐ

    /** OpenVR の代わり（断る回数を決められる）。 */
    struct FakeOverlay {
        double alpha = 0;
        bool shown = false;
        int failAlpha = 0, failShow = 0, failHide = 0;
        int alphaCalls = 0, showCalls = 0, hideCalls = 0;
        /**
         * 透明度を受け取る。
         * @param value 透明度
         * @return 受け付けたら true
         */
        bool setAlpha(double value) {
            ++alphaCalls;
            if (failAlpha > 0) {
                --failAlpha;
                return false;
            }
            alpha = value;
            return true;
        }
        /**
         * 表示・非表示を受け取る。
         * @param show 表示なら true
         * @return 受け付けたら true
         */
        bool setVisible(bool show) {
            int& failures = show ? failShow : failHide;
            if (show) ++showCalls; else ++hideCalls;
            if (failures > 0) {
                --failures;
                return false;
            }
            shown = show;
            return true;
        }
    } backend;
    PanelPresentationState state;
    /**
     * 透明度を反映する。
     */
    const auto apply = [&](double alpha) {
        return state.apply(alpha, [&](double value) { return backend.setAlpha(value); },
                           [&](bool show) { return backend.setVisible(show); });
    };
    backend.failShow = 1;
    CHECK(apply(0.9));
    CHECK(!backend.shown);
    CHECK(!apply(0.9));
    CHECK(backend.shown && backend.showCalls == 2);
    for (int i = 0; i < 120; ++i) CHECK(!apply(0.9));
    CHECK(backend.showCalls == 2 && backend.alphaCalls == 1);  // うまくいったものは何度も渡さない

    backend.failHide = 1;
    CHECK(apply(0));
    CHECK(backend.shown);
    NEAR(backend.alpha, 0);
    CHECK(!apply(0));
    CHECK(!backend.shown && backend.hideCalls == 2);

    backend.failAlpha = 1;
    CHECK(apply(0.9));
    CHECK(!backend.shown);  // 透明度を断られたら、古い透明度のまま出さない
    CHECK(!apply(0.9));
    CHECK(backend.shown);
    NEAR(backend.alpha, 0.9);

    backend.failAlpha = 1;
    CHECK(apply(0));
    CHECK(!backend.shown);  // 透明度 0 を断られても、隠すのは試す
    CHECK(!apply(0));
    NEAR(backend.alpha, 0);

    backend.failShow = 100;
    for (int i = 0; i < 100; ++i) CHECK(apply(0.9));  // 断られ続けても覚えた状態は壊れない
    CHECK(!backend.shown);
    CHECK(!apply(0.9));
    CHECK(backend.shown);
    backend.failAlpha = 1;
    CHECK(apply(0.4));
    NEAR(backend.alpha, 0.9);
    CHECK(!apply(0.9));  // 目標が、最後に受け付けられた値に戻った
    NEAR(backend.alpha, 0.9);
}

/** 時計の文字列。 */
void clockCases() {
    std::tm t {};
    CHECK(formatClock(t, 12) == "12:00 AM");
    CHECK(formatClock(t, 24) == "00:00");
    t.tm_hour = 12;
    t.tm_min = 5;
    CHECK(formatClock(t, 12) == "12:05 PM");
    t.tm_hour = 23;
    t.tm_min = 59;
    CHECK(formatClock(t, 12) == "11:59 PM");
    CHECK(formatClock(t, 24) == "23:59");
    CHECK(formatClock(t, 0).empty());
}

/**
 * 設定の操作と、設定ファイルの読み書き。
 * @param file 一時的な設定ファイルのパス
 */
void configuration(const std::string& file) {
    Config c;
    std::vector<std::string> warnings;
    std::string error;
    {
        std::ofstream out(file);
        out << R"({"visible":false,"position":{"x":0.2,"y":-0.1,"z":-0.7}})";
    }
    CHECK(loadConfig(file, c, warnings, error));
    CHECK(c.attachment == Attachment::Head && !c.visible);
    NEAR(c.posX, 0.2);
    CHECK(c.clockFormat == 24 && c.wristFade);
    NEAR(c.wristFadeEndDeg, 75);

    // 頭: 位置の十字は 2cm、近く / 遠くは見える方向のまま 5cm
    CHECK(applySettingsAction(SettingsAction::MoveRight, c));
    NEAR(c.posX, 0.22);
    CHECK(applySettingsAction(SettingsAction::MoveNear, c));
    NEAR(c.posZ, -0.65);
    const WristPose standard;
    CHECK(!(c.leftWrist != standard) && !(c.rightWrist != standard));

    // 左手: 同じボタンで、パネル自身の軸に 1cm ずつ・左手の値だけを動かす。既定の向き（pitch −90）では
    // パネルの右 = コントローラーの +x、上 = −z、面の向き（近く）= +y
    CHECK(applySettingsAction(SettingsAction::AttachLeft, c));
    const double headX = c.posX, headZ = c.posZ;
    CHECK(applySettingsAction(SettingsAction::MoveRight, c));
    CHECK(applySettingsAction(SettingsAction::MoveUp, c));
    CHECK(applySettingsAction(SettingsAction::MoveNear, c));
    CHECK(applySettingsAction(SettingsAction::MoveFar, c));
    CHECK(applySettingsAction(SettingsAction::MoveFar, c));
    NEAR(c.leftWrist.x, 0.01);
    NEAR(c.leftWrist.y, 0.04);
    NEAR(c.leftWrist.z, 0.07);
    CHECK(applySettingsAction(SettingsAction::PitchDown, c));  // 1° 刻み
    NEAR(c.leftWrist.pitch, -91);
    NEAR(c.posX, headX);
    NEAR(c.posZ, headZ);
    const WristPose left = c.leftWrist;

    // 右手: 左手の値は変わらない
    CHECK(applySettingsAction(SettingsAction::AttachRight, c));
    CHECK(applySettingsAction(SettingsAction::MoveDown, c));
    CHECK(applySettingsAction(SettingsAction::YawRight, c, 5));
    NEAR(c.rightWrist.z, 0.09);  // ↓ 下 = パネルの上の逆 = コントローラーの +z
    NEAR(c.rightWrist.yaw, 5);
    CHECK(!(c.leftWrist != left));

    // 頭の回転と手首の回転は、互いに影響しない
    const WristPose right = c.rightWrist;
    CHECK(applySettingsAction(SettingsAction::AttachHead, c));
    CHECK(applySettingsAction(SettingsAction::PitchUp, c, 5));
    CHECK(applySettingsAction(SettingsAction::YawRight, c, 5));
    CHECK(applySettingsAction(SettingsAction::RollLeft, c));
    NEAR(c.pitchDeg, 5);
    NEAR(c.yawDeg, 5);
    NEAR(c.rollDeg, 1);
    CHECK(!(c.rightWrist != right) && !(c.leftWrist != left));
    CHECK(applySettingsAction(SettingsAction::AttachRight, c));
    CHECK(applySettingsAction(SettingsAction::PitchUp, c, 5));
    CHECK(applySettingsAction(SettingsAction::RollRight, c, 5));
    NEAR(c.rightWrist.pitch, right.pitch + 5);
    NEAR(c.rightWrist.roll, -5);
    NEAR(c.pitchDeg, 5);
    NEAR(c.rollDeg, 1);
    CHECK(!(c.leftWrist != left));

    // 左手と右手で、同じボタンは設定の同じ値を同じ向きに動かす
    for (const SettingsAction action :
         {SettingsAction::MoveLeft, SettingsAction::MoveRight, SettingsAction::MoveUp, SettingsAction::MoveDown,
          SettingsAction::MoveNear, SettingsAction::MoveFar, SettingsAction::YawLeft, SettingsAction::YawRight,
          SettingsAction::PitchUp, SettingsAction::PitchDown, SettingsAction::RollLeft, SettingsAction::RollRight}) {
        Config hands;
        hands.attachment = Attachment::LeftWrist;
        CHECK(applySettingsAction(action, hands, 5));
        hands.attachment = Attachment::RightWrist;
        CHECK(applySettingsAction(action, hands, 5));
        CHECK(!(hands.leftWrist != hands.rightWrist));
    }

    // 手首の標準の位置: 選んでいる手だけ既定に戻す
    CHECK(applySettingsAction(SettingsAction::PresetWrist, c));
    CHECK(!(c.rightWrist != standard));
    CHECK(!(c.leftWrist != left));
    CHECK(!applySettingsAction(SettingsAction::PresetWrist, c));  // もう標準なので変わらない

    // 保存して読み直す（新しいキーも含めて、知らないキーの警告なし）
    c.updateCheck = false;
    c.clockFormat = 12;
    c.wristFade = false;
    c.wristFadeEndDeg = 80;
    CHECK(saveConfig(file, c, error));
    Config roundTrip;
    warnings.clear();
    CHECK(loadConfig(file, roundTrip, warnings, error));
    CHECK(warnings.empty());
    CHECK(roundTrip.attachment == Attachment::RightWrist && roundTrip.clockFormat == 12);
    CHECK(!(roundTrip.leftWrist != c.leftWrist) && !(roundTrip.rightWrist != c.rightWrist));
    CHECK(!roundTrip.wristFade);
    NEAR(roundTrip.wristFadeEndDeg, 80);
    CHECK(!roundTrip.updateCheck);
    NEAR(roundTrip.pitchDeg, 5);
    NEAR(roundTrip.yawDeg, 5);
    NEAR(roundTrip.rollDeg, 1);
    NEAR(roundTrip.posZ, headZ);

    // 範囲外・型違いは警告して丸める（手首の pitch は −180〜0）
    {
        std::ofstream out(file);
        out << R"({"attachment":"bad","clock_format":12.5,"left_wrist":{"x":9,"pitch":-300},)"
            << R"("right_wrist":{"pitch":20,"roll":400,"w":1},"wrist_fade_end_deg":5})";
    }
    warnings.clear();
    CHECK(loadConfig(file, c, warnings, error));
    CHECK(warnings.size() >= 7);
    CHECK(c.attachment == Attachment::Head && c.clockFormat == 24);
    NEAR(c.leftWrist.x, 0.5);
    NEAR(c.leftWrist.pitch, -180);
    NEAR(c.rightWrist.pitch, 0);
    NEAR(c.rightWrist.roll, 180);
    NEAR(c.wristFadeEndDeg, 35);

    // 手首の位置は ±50cm、pitch は −180〜0、消える角度は 35〜90° で止まる
    CHECK(applySettingsAction(SettingsAction::AttachLeft, c));
    CHECK(!applySettingsAction(SettingsAction::MoveRight, c));
    NEAR(c.leftWrist.x, 0.5);
    CHECK(!applySettingsAction(SettingsAction::PitchDown, c, 5));
    NEAR(c.leftWrist.pitch, -180);
    CHECK(applySettingsAction(SettingsAction::AttachRight, c));
    CHECK(!applySettingsAction(SettingsAction::PitchUp, c, 5));
    NEAR(c.rightWrist.pitch, 0);
    CHECK(!applySettingsAction(SettingsAction::FadeAngleDown, c));
    c.wristFadeEndDeg = 90;
    CHECK(!applySettingsAction(SettingsAction::FadeAngleUp, c));
    CHECK(applySettingsAction(SettingsAction::FadeAngleDown, c));
    NEAR(c.wristFadeEndDeg, 85);
    CHECK(!applySettingsAction(SettingsAction::Clock24, c));  // もう 24h
    CHECK(applySettingsAction(SettingsAction::Clock12, c) && c.clockFormat == 12);
    CHECK(applySettingsAction(SettingsAction::ClockOff, c) && c.clockFormat == 0);

    // 壊れた JSON は前の設定のまま
    {
        std::ofstream out(file);
        out << "{invalid";
    }
    const WristPose before = c.leftWrist;
    CHECK(!loadConfig(file, c, warnings, error));
    CHECK(!(before != c.leftWrist));

    // 既定に戻す: 固定先・両手・傾けると消す・時計も戻す
    resetDisplaySettings(c);
    CHECK(c.attachment == Attachment::Head && c.clockFormat == 24 && c.wristFade);
    CHECK(!(c.leftWrist != standard) && !(c.rightWrist != standard));
    NEAR(c.pitchDeg, 0);
    NEAR(c.yawDeg, 0);
    NEAR(c.rollDeg, 0);
}

/**
 * 性能パネルの高さ: 時計を出すと General の時計の行ぶん高く、出さない（0）と 1 行（22 px）詰まる。
 * 既定のレイアウトは 1 列なので、幅は 512 のまま。
 * @param directory PNG を書く一時フォルダ
 */
void panelHeights(const std::string& directory) {
    Config c;
    FontSet fonts;
    fonts.load(c.fontPath, c.boldFontPath);
    frame_perf_overlay::GridRenderer renderer(fonts);
    // 時計の行の分だけ高さが変わるのを確かめるので、既定のウィジェットを全部 1 列に並べて描く
    frame_perf_overlay::Layout oneColumn = c.layout;
    for (size_t i = 1; i < oneColumn.columns.size(); ++i) {
        for (const auto& w : oneColumn.columns[i].widgets) oneColumn.columns[0].widgets.push_back(w);
    }
    oneColumn.columns.resize(1);
    renderer.setLayout(oneColumn);
    frame_perf_overlay::MetricStore store;
    int withClock = 0;
    for (const int format : {24, 12, 0}) {
        frame_perf_overlay::WidgetContext ctx;
        ctx.store = &store;
        ctx.now = 1.0;
        ctx.clockFormat = format;
        renderer.updateGeometry(ctx);
        renderer.render(ctx);
        const int width = renderer.geometry().textureWidth;
        const int height = renderer.geometry().textureHeight;
        if (format == 24) withClock = height;
        const int expected = format != 0 ? withClock : withClock - 22;
        CHECK(width == 512 && height == expected);
        CHECK(renderer.toRgba().size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        // PNG は見える部分の高さで書き出す
        const std::string path = directory + "/panel-" + std::to_string(format) + ".png";
        CHECK(renderer.writePng(path));
        cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
        CHECK(cairo_image_surface_get_width(png) == 512 && cairo_image_surface_get_height(png) == expected);
        cairo_surface_destroy(png);
    }
}

/** ボタン 1 つの期待する場所（設定パネルの画像の px）。 */
struct ExpectedButton {
    SettingsAction action;
    double x, y, w, h;
};

/**
 * 画像の 1 画素の色（RGB）。
 * @param rgba 設定パネルの非乗算済み RGBA
 * @param width 画像の幅
 * @param x 左端からの px
 * @param y 上端からの px
 * @return 0xRRGGBB
 */
unsigned pixel(const std::vector<uint8_t>& rgba, int width, double x, double y) {
    const size_t i = (static_cast<size_t>(y) * width + static_cast<size_t>(x)) * 4;
    return (static_cast<unsigned>(rgba[i]) << 16) | (static_cast<unsigned>(rgba[i + 1]) << 8) | rgba[i + 2];
}

/**
 * 色を 0xRRGGBB にする。
 * @param c 色
 * @return 0xRRGGBB
 */
unsigned hex(Color c) {
    return (static_cast<unsigned>(std::lround(c.r * 255)) << 16) | (static_cast<unsigned>(std::lround(c.g * 255)) << 8) |
           static_cast<unsigned>(std::lround(c.b * 255));
}

/**
 * どのボタンも、真ん中を押すとその操作になり、その場所にボタンの形が描かれていて（地がカードの色でない）、
 * すぐ上はカードの地のまま（ボタンの外）であることを確かめる。当たり判定と見た目のずれを見つけるため。
 * @param panel 設定パネル（描いたあと）
 * @param buttons 期待するボタン
 * @param now 押した時刻
 */
void checkButtons(SettingsPanel& panel, const std::vector<ExpectedButton>& buttons, double now) {
    const std::vector<uint8_t> rgba = panel.toRgba();
    for (const auto& b : buttons) {
        const std::string where = " at (" + std::to_string(static_cast<int>(b.x)) + ", " + std::to_string(static_cast<int>(b.y)) + ")";
        // 見た目: 左の縁の少し内側はボタン（地の色かアクセント）、すぐ上と下はカードの地
        const unsigned inside = pixel(rgba, panel.width(), b.x + 3, b.y + b.h / 2);
        if (inside == hex(kCard) || inside == hex(kBg)) throw std::runtime_error("button not drawn" + where);
        if (pixel(rgba, panel.width(), b.x + b.w / 2, b.y - 4) != hex(kCard)) throw std::runtime_error("above not card" + where);
        if (pixel(rgba, panel.width(), b.x + b.w / 2, b.y + b.h + 4) != hex(kCard)) throw std::runtime_error("below not card" + where);
        // 当たり判定: 真ん中と、縁の少し内側
        const SettingsAction centre = panel.pointerDown(b.x + b.w / 2, b.y + b.h / 2, now);
        if (centre != b.action && b.action != SettingsAction::AngleStep1 && b.action != SettingsAction::AngleStep5) {
            throw std::runtime_error("hit test" + where);
        }
        if (panel.pointerDown(b.x + 2, b.y + 2, now) != centre) throw std::runtime_error("hit edge" + where);
        if (centre != SettingsAction::None && panel.pointerDown(b.x + b.w / 2, b.y - 3, now) == centre) {
            throw std::runtime_error("hit above" + where);
        }
    }
    panel.pointerLeave();
}

/**
 * 設定パネル: 別の画面は無く、固定先のセグメントと、頭でも手首でも同じ場所の十字・向きのボタン。
 * @param directory PNG を書く一時フォルダ
 */
void controls(const std::string& directory) {
    Config c;
    c.language = Language::En;
    FontSet fonts;
    fonts.load(c.fontPath, c.boldFontPath);
    SettingsPanel panel(fonts);
    const AutostartStatus service {AutostartStatus::State::Enabled, false, false};
    CHECK(panel.width() == 1600 && panel.height() <= 760);

    // どの固定先でも同じ場所にあるボタン
    const std::vector<ExpectedButton> common = {
        // パネル
        {SettingsAction::ShowOn, 168, 188, 130, 68}, {SettingsAction::ShowOff, 298, 188, 130, 68},
        {SettingsAction::SizeDown, 168, 267, 84, 68}, {SettingsAction::SizeUp, 372, 267, 84, 68},
        {SettingsAction::AlphaDown, 168, 346, 84, 68}, {SettingsAction::AlphaUp, 372, 346, 84, 68},
        {SettingsAction::ClockOff, 168, 425, 96, 68}, {SettingsAction::Clock12, 264, 425, 96, 68},
        {SettingsAction::Clock24, 360, 425, 96, 68}, {SettingsAction::Reset, 168, 504, 288, 68},
        // 位置: 固定先・十字・近く / 遠く
        {SettingsAction::AttachHead, 630, 188, 410.0 / 4, 68},
        {SettingsAction::AttachLeft, 630 + 410.0 / 4, 188, 410.0 / 4, 68},
        {SettingsAction::AttachRight, 630 + 820.0 / 4, 188, 410.0 / 4, 68},
        {SettingsAction::AttachDetached, 630 + 1230.0 / 4, 188, 410.0 / 4, 68},
        {SettingsAction::MoveUp, 637, 425, 106, 68}, {SettingsAction::MoveLeft, 520, 504, 106, 68},
        {SettingsAction::MoveDown, 637, 504, 106, 68}, {SettingsAction::MoveRight, 754, 504, 106, 68},
        {SettingsAction::MoveNear, 870, 425, 170, 68}, {SettingsAction::MoveFar, 870, 504, 170, 68},
        // 向き: 十字と刻み
        {SettingsAction::RollLeft, 1104, 188, 142, 68}, {SettingsAction::PitchUp, 1257, 188, 142, 68},
        {SettingsAction::RollRight, 1410, 188, 142, 68}, {SettingsAction::YawLeft, 1104, 267, 142, 68},
        {SettingsAction::PitchDown, 1257, 267, 142, 68}, {SettingsAction::YawRight, 1410, 267, 142, 68},
        {SettingsAction::AngleStep1, 1104, 346, 224, 68}, {SettingsAction::AngleStep5, 1328, 346, 224, 68},
    };
    const std::vector<ExpectedButton> headOnly = {
        {SettingsAction::PresetLeftTop, 520, 267, 166, 68}, {SettingsAction::PresetRightTop, 874, 267, 166, 68},
        {SettingsAction::PresetLeftBottom, 520, 346, 166, 68}, {SettingsAction::PresetCenterBottom, 697, 346, 166, 68},
        {SettingsAction::PresetRightBottom, 874, 346, 166, 68},
        {SettingsAction::FaceMe, 1104, 425, 218.5, 68}, {SettingsAction::FaceForward, 1333.5, 425, 218.5, 68},
    };
    const std::vector<ExpectedButton> wristOnly = {
        {SettingsAction::PresetWrist, 520, 267, 520, 68},
        {SettingsAction::FadeOn, 1292, 425, 130, 68}, {SettingsAction::FadeOff, 1422, 425, 130, 68},
        {SettingsAction::FadeAngleDown, 1264, 504, 84, 68}, {SettingsAction::FadeAngleUp, 1468, 504, 84, 68},
    };

    for (const Language language : {Language::En, Language::Ja}) {
        c = Config();
        c.language = language;
        const std::string code = language == Language::En ? "en" : "ja";
        // 頭
        panel.render(c, service, {});
        checkButtons(panel, common, 1);
        checkButtons(panel, headOnly, 1);
        CHECK(panel.pointerDown(780, 301, 1) == SettingsAction::None);   // 上の段の真ん中は空き
        CHECK(panel.pointerDown(1200, 538, 1) == SettingsAction::None);  // 向きのいちばん下の段は空き
        panel.pointerLeave();
        panel.render(c, service, {});
        CHECK(panel.writePng(directory + "/head-" + code + ".png"));
        // 左手・右手: 同じ場所の十字と向きのボタンのまま、位置のボタンといちばん下の段だけ変わる
        for (const SettingsAction attach : {SettingsAction::AttachLeft, SettingsAction::AttachRight}) {
            CHECK(panel.pointerDown(attach == SettingsAction::AttachLeft ? 784 : 886, 222, 2) == attach);
            CHECK(applySettingsAction(attach, c));
            panel.pointerLeave();
            panel.render(c, service, {});
            checkButtons(panel, common, 3);
            checkButtons(panel, wristOnly, 3);
            CHECK(panel.pointerDown(1200, 459, 3) == SettingsAction::None);  // 自分に向ける は無い
            CHECK(panel.pointerDown(600, 380, 3) == SettingsAction::None);   // 頭の位置のボタンは無い
            panel.pointerLeave();
            panel.render(c, service, {});
            CHECK(panel.writePng(directory + "/" + (attach == SettingsAction::AttachLeft ? "left-" : "right-") + code + ".png"));
        }
        // 頭に戻すと、頭のボタンに戻る
        CHECK(applySettingsAction(SettingsAction::AttachHead, c));
        panel.render(c, service, {});
        checkButtons(panel, headOnly, 4);
    }

    // 刻みの切り替えはパネルの中だけ
    c = Config();
    c.language = Language::En;
    panel.render(c, service, {});
    CHECK(panel.pointerDown(1440, 380, 5) == SettingsAction::None);
    NEAR(panel.angleStepDeg(), 5);
    CHECK(panel.pointerDown(1200, 380, 5) == SettingsAction::None);
    NEAR(panel.angleStepDeg(), 1);

    // 更新の帯（新しい版・確認中）が出ていても、固定先・時計・向きのボタンを横取りしない
    frame_updater::UpdateStatus update;
    update.state = frame_updater::UpdateState::Available;
    update.current = "0.2.0";
    update.latest = "0.3.0";
    update.installable = true;
    c.attachment = Attachment::LeftWrist;
    panel.render(c, service, update);
    CHECK(panel.pointerDown(886, 222, 6) == SettingsAction::AttachRight);
    CHECK(panel.pointerDown(400, 459, 6) == SettingsAction::Clock24);
    panel.armUpdateConfirmForPreview();
    panel.render(c, service, update);
    CHECK(panel.pointerDown(1320, 222, 6) == SettingsAction::PitchUp);
    CHECK(panel.pointerDown(1340, 459, 6) == SettingsAction::FadeOn);
    panel.pointerLeave();
}

/** Room (Detached): its buttons sit where they are laid out, curve steps by 10 degrees within 0..90, and an unplaced pose isn't moved. */
void roomControls(const std::string& directory) {
    Config c;
    c.language = Language::En;
    c.attachment = Attachment::Detached;
    FontSet fonts;
    fonts.load(c.fontPath, c.boldFontPath);
    SettingsPanel panel(fonts);
    const AutostartStatus service {AutostartStatus::State::Enabled, false, false};
    panel.render(c, service, {});
    const std::vector<ExpectedButton> room = {
        {SettingsAction::BringInFront, 520, 267, 520, 68},
        {SettingsAction::FaceMe, 1104, 425, 448, 68},
        {SettingsAction::CurveDown, 1264, 504, 84, 68}, {SettingsAction::CurveUp, 1468, 504, 84, 68},
        {SettingsAction::AttachDetached, 630 + 1230.0 / 4, 188, 410.0 / 4, 68},
    };
    checkButtons(panel, room, 1);
    panel.pointerLeave();
    CHECK(panel.writePng(directory + "/room-en.png"));

    // The curve steps by 10 and clamps to 0..90
    CHECK(!applySettingsAction(SettingsAction::CurveDown, c));
    CHECK(c.detachedCurveDeg == 0);
    for (int i = 1; i <= 9; ++i) {
        CHECK(applySettingsAction(SettingsAction::CurveUp, c));
        CHECK(c.detachedCurveDeg == 10 * i);
    }
    CHECK(!applySettingsAction(SettingsAction::CurveUp, c));
    CHECK(c.detachedCurveDeg == 90);
    CHECK(applySettingsAction(SettingsAction::CurveDown, c));
    CHECK(c.detachedCurveDeg == 80);

    // Move / turn / presets on a pose that was never placed change nothing
    c.detachedCurveDeg = 0;
    CHECK(!c.detachedPose.valid);
    const Config before = c;
    for (const SettingsAction a : {SettingsAction::MoveLeft, SettingsAction::MoveUp, SettingsAction::MoveNear,
                                   SettingsAction::YawRight, SettingsAction::PitchUp, SettingsAction::RollLeft,
                                   SettingsAction::FaceMe, SettingsAction::PresetLeftTop, SettingsAction::BringInFront}) {
        CHECK(!applySettingsAction(a, c));
        CHECK(c.detachedPose == before.detachedPose && c.attachment == Attachment::Detached);
        CHECK(c.posX == before.posX && c.posY == before.posY && c.posZ == before.posZ && c.yawDeg == before.yawDeg);
    }
    // On a placed pose a move button does move it
    c.detachedPose.valid = true;
    CHECK(applySettingsAction(SettingsAction::MoveRight, c));
    CHECK(c.detachedPose.m[0][3] > 0);
    CHECK(!applySettingsAction(SettingsAction::FaceMe, c));  // the caller does Face me (it needs the HMD)
}

}  // namespace

/**
 * テストを全部動かす。
 * @return 0 = 全部通った、1 = 失敗、2 = 一時フォルダを作れない
 */
int main() {
    char directory[] = "/tmp/frame-overlay-test-XXXXXX";
    if (mkdtemp(directory) == nullptr) return 2;
    try {
        geometry();
        panelAxisNudges();
        pollScheduling();
        placementRecovery();
        clockCases();
        configuration(std::string(directory) + "/config.json");
        panelHeights(directory);
        controls(directory);
        roomControls(directory);
        std::cout << "Geometry, rotation convention, panel-axis nudges, polling, placement recovery, clock, config, panel heights, UI controls and Room controls passed.\n";
        std::filesystem::remove_all(directory);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (artifacts: " << directory << ")\n";
        return 1;
    }
    return 0;
}
