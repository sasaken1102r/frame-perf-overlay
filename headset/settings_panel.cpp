// 設定パネルの実装。
#include "settings_panel.h"

#include "detached.h"  // Room
#include "draw.h"
#include "i18n.h"
#include "placement.h"
#include "theme.h"

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// 1600×738。1px あたりの大きさは前（1200px を 2.8m）と同じにする（幅は vr_overlay.cpp で px から決める）。
// 見出しの行に新しい版の確認の帯を入れ、パネル・位置・向きのカードを横に 3 つ並べる。
// 3 つのカードは同じ高さの 5 段（ボタン 68px ＋ すき間 11px）にそろえる
constexpr int kWidth = 1600;
constexpr double kPad = 28;          // パネルの外側の余白
constexpr double kButtonH = 68;      // ボタンの高さ（レーザーで押しやすい大きさ。前と同じ）
constexpr double kHeaderY = 20;      // 見出しの行（見出し・新しい版の確認の帯・状態のピル）の上端
constexpr double kHeaderH = kButtonH + 16;  // 帯のボタンも 68px にして、上下に 8px ずつ
constexpr double kCardGap = 24;      // カードどうしの間
constexpr double kCardY = kHeaderY + kHeaderH + 20;  // 3 つのカードの上端
constexpr double kCardPad = 20;      // カードの中の余白
constexpr double kGap = 11;          // ボタンどうしの間（縦も横も）
constexpr int kRows = 5;             // カードの中の段の数
constexpr double kRowY0 = kCardY + 64;         // 1 段目のボタンの上端（上は見出し）
constexpr double kRowStep = kButtonH + kGap;   // 段の間隔
constexpr double kCardH = 64 + kRows * kButtonH + (kRows - 1) * kGap + kCardPad;
// 左のカード「パネル」: 行の見出し（左）とボタン（右）
constexpr double kLeftCardX = kPad;
constexpr double kLeftCardW = 448;
constexpr double kRowControlX = kLeftCardX + 140;  // ボタンの左端（左は行の見出し）
constexpr double kSegmentW = 260;                  // 2 択のセグメント切り替えの幅
constexpr double kStepButtonW = 84;                // − / ＋
constexpr double kValueW = 120;                    // − と ＋ の間の値
constexpr double kControlW = kStepButtonW * 2 + kValueW;  // − 値 ＋ の幅（3 択の時計・既定に戻すも同じ幅）
// 真ん中のカード「位置」
constexpr double kPosCardX = kLeftCardX + kLeftCardW + kCardGap;
constexpr double kPosCardW = 560;
constexpr double kPosInnerX = kPosCardX + kCardPad;
constexpr double kPosInnerW = kPosCardW - kCardPad * 2;
constexpr double kAttachLabelW = 110;              // 「固定先」の見出しの幅
constexpr double kPresetW = (kPosInnerW - 2 * kGap) / 3;  // 位置のボタン 1 つ（3 列）
constexpr double kArrowW = 106;                    // 微調整の十字の 1 つ
constexpr double kDepthW = 170;                    // 近く / 遠く
constexpr double kDepthX = kPosCardX + kPosCardW - kCardPad - kDepthW;
// 右のカード「向き」: 上に十字（回す・上下・左右）、その下に刻みの 1° / 5°、いちばん下の段は固定先で変わる
constexpr double kFacingCardX = kPosCardX + kPosCardW + kCardGap;
constexpr double kFacingCardW = kWidth - kPad - kFacingCardX;
constexpr double kFacingInnerX = kFacingCardX + kCardPad;
constexpr double kFacingInnerW = kFacingCardW - kCardPad * 2;
constexpr double kFacingArrowW = (kFacingInnerW - 2 * kGap) / 3;  // 十字の 1 つ
// 下の段
constexpr double kFooterY = kCardY + kCardH + kCardGap;
constexpr int kHeight = static_cast<int>(kFooterY + kButtonH + 54);  // 下に 1 行の説明
constexpr double kFooterSegmentW = 240;
constexpr double kQuitW = 230;
// The Layout page: the lanes on the left, the palette on the right (same height as the placement cards)
constexpr double kEditorCardW = 1000;
constexpr double kPaletteCardX = kPad + kEditorCardW + kCardGap;
constexpr double kPageSwitchW = 420;  // three pages
// The Profiles page: the list (left), the selected profile's actions (right)
constexpr double kProfListW = 640;
constexpr double kProfRowH = 44;
constexpr double kProfRowGap = 4;
constexpr double kProfRowY0 = kCardY + 64;
constexpr double kProfCard2X = kPad + kProfListW + kCardGap;
constexpr double kProfCard2W = kWidth - kPad - kProfCard2X;

constexpr double kNudgeM = 0.02;       // 頭: 上下左右の微調整（m）
constexpr double kDepthStepM = 0.05;   // 頭: 前後の微調整（m）
constexpr double kPresetDistance = 0.5;  // 頭: プリセットの座標はこの距離での値
constexpr double kWristNudgeM = 0.01;  // 手首: 上下左右・前後の微調整（m）
constexpr double kWristOffsetLimitM = 0.5;  // 手首: コントローラーから離せる範囲（±m）
constexpr double kFadeAngleStepDeg = 5;     // 消える角度の刻み

/**
 * 段の上端。
 * @param row 段（0 から）
 * @return y（px）
 */
constexpr double rowY(int row) {
    return kRowY0 + row * kRowStep;
}

/** 位置のプリセット（距離 0.5m での x, y）。 */
struct Preset {
    SettingsAction action;
    double x, y;
};
constexpr Preset kPresets[] = {
    {SettingsAction::PresetLeftBottom, -0.15, -0.12},  {SettingsAction::PresetCenterBottom, 0.0, -0.16},
    {SettingsAction::PresetRightBottom, 0.15, -0.12},  {SettingsAction::PresetLeftTop, -0.15, 0.12},
    {SettingsAction::PresetRightTop, 0.15, 0.12},
};

/**
 * 1mm 単位に丸める（小数の誤差を設定ファイルに残さない）。
 * @param meters 値（m）
 * @return 丸めた値
 */
double roundMm(double meters) {
    return std::round(meters * 1000.0) / 1000.0;
}

/**
 * 手首の位置を 1 つの向きに動かす（1mm 単位に丸め、±50cm に収める）。
 * @param meters 今の値（m）
 * @param delta 動かす量（m）
 * @return 新しい値（m）
 */
double nudgeWrist(double meters, double delta) {
    return std::clamp(roundMm(meters + delta), -kWristOffsetLimitM, kWristOffsetLimitM);
}

/**
 * 手首のパネルを、パネル自身の軸（今の回転をかけたあとの右・上・面の向き）で動かす。設定ファイルに保存するのは
 * コントローラーの座標のままなので、パネルの軸の 1 歩を回転行列（wristTransform の左 3 列）でコントローラーの座標の
 * 差分に直して足す。各軸は 1mm 単位に丸めて ±50cm に収める。
 * @param pose 書き換える手首の位置と向き
 * @param axis パネルの軸（0 = 右、1 = 上、2 = 面の向き = 見ている人の方）
 * @param direction +1 でその軸の向きへ、-1 で逆へ
 */
void nudgeWristAlongPanel(WristPose& pose, int axis, int direction) {
    const vr::HmdMatrix34_t m = wristTransform(pose);
    const double step = direction * kWristNudgeM;
    pose.x = nudgeWrist(pose.x, m.m[0][axis] * step);
    pose.y = nudgeWrist(pose.y, m.m[1][axis] * step);
    pose.z = nudgeWrist(pose.z, m.m[2][axis] * step);
}

/**
 * 手首の位置が、標準の位置からどれだけ離れているか（向きによらない距離）。
 * @param pose 手首の位置と向き
 * @return 距離（m）
 */
double wristDistanceFromStandard(const WristPose& pose) {
    const WristPose standard;
    return std::sqrt((pose.x - standard.x) * (pose.x - standard.x) + (pose.y - standard.y) * (pose.y - standard.y) +
                     (pose.z - standard.z) * (pose.z - standard.z));
}

/**
 * パネルまでの距離に合わせて、プリセットの座標を伸び縮みさせる倍率。
 * @param config 設定
 * @return 倍率（z = -0.5m で 1）
 */
double distanceScale(const Config& config) {
    return std::max(0.1, -config.posZ) / kPresetDistance;
}

/**
 * 今の位置がプリセットと同じか。
 * @param preset プリセット
 * @param config 設定
 * @return 同じなら true
 */
bool matchesPreset(const Preset& preset, const Config& config) {
    const double scale = distanceScale(config);
    return std::fabs(config.posX - preset.x * scale) < 0.004 && std::fabs(config.posY - preset.y * scale) < 0.004;
}

/**
 * 手首の位置と向きが標準（WristPose の既定値）のままか。
 * @param pose 手首の位置と向き
 * @return ほぼ同じ（0.5mm・0.05° 未満の差）なら true
 */
bool matchesWristPreset(const WristPose& pose) {
    const WristPose standard;
    return std::fabs(pose.x - standard.x) < 0.0005 && std::fabs(pose.y - standard.y) < 0.0005 &&
           std::fabs(pose.z - standard.z) < 0.0005 && std::fabs(pose.pitch - standard.pitch) < 0.05 &&
           std::fabs(pose.yaw - standard.yaw) < 0.05 && std::fabs(pose.roll - standard.roll) < 0.05;
}

/**
 * 符号つきの cm 表記にする（例: −15）。
 * @param meters 値（m）
 * @return 文字列
 */
std::string signedCm(double meters) {
    char text[32];
    const long cm = std::lround(meters * 100.0);
    std::snprintf(text, sizeof(text), "%s%ld", cm < 0 ? "−" : "", std::labs(cm));
    return text;
}

/**
 * 角度を刻みの次の目盛りへ進める（例: 16.7° から、1° 刻みなら増やすと 17°・減らすと 16°、
 * 5° 刻みなら増やすと 20°・減らすと 15°）。範囲の外には出さない。
 * @param degrees 今の角度（度）
 * @param direction +1 で増やす、-1 で減らす
 * @param limit 範囲（±limit）
 * @param step 刻み（度）
 * @return 新しい角度（度）
 */
double stepAngle(double degrees, int direction, double limit, double step) {
    const double steps = degrees / step;
    const double next = direction > 0 ? std::floor(steps + 1e-6) + 1 : std::ceil(steps - 1e-6) - 1;
    return std::clamp(next * step, -limit, limit);
}

/**
 * 手首の pitch を刻みの次の目盛りへ進める。手首の pitch は「寝かせた向き」の −90 を中心に −180〜0 なので、
 * 中心からの角度（頭の pitch と同じ ±90）に直してから stepAngle で進める。
 * @param degrees 今の pitch（度）
 * @param direction +1 で増やす（面を上へ）、-1 で減らす
 * @param step 刻み（度）
 * @return 新しい pitch（度）
 */
double stepWristPitch(double degrees, int direction, double step) {
    return stepAngle(degrees + 90.0, direction, 90.0, step) - 90.0;
}

/**
 * 符号つきの度の表記にする（例: −17°）。
 * @param degrees 値（度）
 * @return 文字列
 */
std::string signedDegrees(double degrees) {
    char text[32];
    const long whole = std::lround(degrees);
    std::snprintf(text, sizeof(text), "%s%ld°", whole < 0 ? "−" : "", std::labs(whole));
    return text;
}

/**
 * 回す向きの絵（すき間が上にある円弧 ＋ 矢じり）を線で描く。画面の上で反時計回りなら ⟲、時計回りなら ⟳。
 * @param cr cairo
 * @param cx 中心の x
 * @param cy 中心の y
 * @param r 半径
 * @param counterclockwise 反時計回り（⟲）なら true
 * @param c 色
 */
void drawRotateArrow(cairo_t* cr, double cx, double cy, double r, bool counterclockwise, Color c) {
    const double deg = M_PI / 180.0;
    // 画面の座標は y が下向きなので、角度が増える向き（cairo_arc）が時計回り。
    // ⟲: 左上（240°）から反時計回りに左・下・右を回って右上（300° = −60°）で終わる。⟳ はその左右反転
    const double start = counterclockwise ? 240 * deg : 300 * deg;
    const double end = counterclockwise ? -60 * deg : 600 * deg;
    cairo_save(cr);
    cairo_set_source_rgb(cr, c.r, c.g, c.b);
    cairo_set_line_width(cr, r * 0.3);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    if (counterclockwise) {
        cairo_arc_negative(cr, cx, cy, r, start, end);
    } else {
        cairo_arc(cr, cx, cy, r, start, end);
    }
    cairo_stroke(cr);
    // 矢じり: 終わりの点で、進む向き（円の接線）に向ける
    const double px = cx + r * std::cos(end);
    const double py = cy + r * std::sin(end);
    const double dx = counterclockwise ? std::sin(end) : -std::sin(end);
    const double dy = counterclockwise ? -std::cos(end) : std::cos(end);
    const double head = r * 0.9;
    cairo_move_to(cr, px + dx * head, py + dy * head);
    cairo_line_to(cr, px - dx * head * 0.35 - dy * head * 0.75, py - dy * head * 0.35 + dx * head * 0.75);
    cairo_line_to(cr, px - dx * head * 0.35 + dy * head * 0.75, py - dy * head * 0.35 - dx * head * 0.75);
    cairo_close_path(cr);
    cairo_fill(cr);
    cairo_restore(cr);
}

/**
 * 回転なし（0, 0, 0）か。
 * @param config 設定
 * @return 3 つとも 0 なら true
 */
bool isFacingForward(const Config& config) {
    return config.yawDeg == 0.0 && config.pitchDeg == 0.0 && config.rollDeg == 0.0;
}

/**
 * 画面で変えられる設定（表示まわり・言語）が同じか。
 * @param a 設定
 * @param b 設定
 * @return 同じなら true
 */
bool sameDisplaySettings(const Config& a, const Config& b) {
    return a.visible == b.visible && a.language == b.language && a.attachment == b.attachment && a.posX == b.posX &&
           a.posY == b.posY && a.posZ == b.posZ && a.yawDeg == b.yawDeg && a.pitchDeg == b.pitchDeg &&
           a.rollDeg == b.rollDeg && !(a.leftWrist != b.leftWrist) && !(a.rightWrist != b.rightWrist) &&
           a.wristFade == b.wristFade && a.wristFadeEndDeg == b.wristFadeEndDeg && a.clockFormat == b.clockFormat &&
           a.widthM == b.widthM && a.alpha == b.alpha && a.detachedPose == b.detachedPose &&
           a.detachedCurveDeg == b.detachedCurveDeg;
}

/** Splits text into lines no wider than maxW (greedy, at spaces). */
std::vector<std::string> wrapText(const Pen& pen, const std::string& text, double size, double maxW, bool bold) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t space = text.find(' ', pos);
        if (space == std::string::npos) space = text.size();
        const std::string word = text.substr(pos, space - pos);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && pen.measure(candidate, size, bold) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        pos = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

/** Shortens a line until it and a trailing "…" fit in maxW (whole UTF-8 characters, trailing spaces dropped). */
std::string withEllipsis(const Pen& pen, std::string line, double size, double maxW, bool bold) {
    while (!line.empty() && pen.measure(line + "…", size, bold) > maxW) {
        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80) line.pop_back();
        if (!line.empty()) line.pop_back();
        while (!line.empty() && line.back() == ' ') line.pop_back();
    }
    return line + "…";
}

}  // namespace

bool applySettingsAction(SettingsAction action, Config& config, double angleStepDeg) {
    const Config before = config;
    // 位置と向きのボタンは、今の固定先の値を動かす（頭は posX など、手首はその手の WristPose）
    const bool wrist = config.attachment == Attachment::LeftWrist || config.attachment == Attachment::RightWrist;
    WristPose& pose = selectedWrist(config);
    // Room: the move / turn buttons move the room pose along the panel's own axes (like the wrist). Face me and
    // Bring it in front need the HMD pose, so the caller does those; the head's presets don't apply
    if (config.attachment == Attachment::Detached) {
        DetachedPose& room = config.detachedPose;
        bool moves = true;  // a move / turn button: never falls through to the head's pose below
        switch (action) {
            case SettingsAction::MoveLeft: room = nudgeDetached(room, 0, -kDetachedNudgeM); break;
            case SettingsAction::MoveRight: room = nudgeDetached(room, 0, kDetachedNudgeM); break;
            case SettingsAction::MoveUp: room = nudgeDetached(room, 1, kDetachedNudgeM); break;
            case SettingsAction::MoveDown: room = nudgeDetached(room, 1, -kDetachedNudgeM); break;
            case SettingsAction::MoveNear: room = nudgeDetached(room, 2, kDetachedDepthStepM); break;
            case SettingsAction::MoveFar: room = nudgeDetached(room, 2, -kDetachedDepthStepM); break;
            case SettingsAction::PitchUp: room = rotateDetached(room, 0, angleStepDeg); break;
            case SettingsAction::PitchDown: room = rotateDetached(room, 0, -angleStepDeg); break;
            case SettingsAction::YawLeft: room = rotateDetached(room, 1, -angleStepDeg); break;
            case SettingsAction::YawRight: room = rotateDetached(room, 1, angleStepDeg); break;
            case SettingsAction::RollLeft: room = rotateDetached(room, 2, angleStepDeg); break;
            case SettingsAction::RollRight: room = rotateDetached(room, 2, -angleStepDeg); break;
            case SettingsAction::FaceMe:
            case SettingsAction::FaceForward:
            case SettingsAction::PresetLeftBottom:
            case SettingsAction::PresetCenterBottom:
            case SettingsAction::PresetRightBottom:
            case SettingsAction::PresetLeftTop:
            case SettingsAction::PresetRightTop:
            case SettingsAction::PresetWrist: break;
            default: moves = false; break;
        }
        if (moves) {
            if (!before.detachedPose.valid) config.detachedPose = before.detachedPose;  // nothing to move yet
            return config.detachedPose != before.detachedPose;
        }
    }
    switch (action) {
        case SettingsAction::ShowOn: config.visible = true; break;
        case SettingsAction::ShowOff: config.visible = false; break;
        case SettingsAction::AttachHead: config.attachment = Attachment::Head; break;
        case SettingsAction::AttachLeft: config.attachment = Attachment::LeftWrist; break;
        case SettingsAction::AttachRight: config.attachment = Attachment::RightWrist; break;
        case SettingsAction::AttachDetached: config.attachment = Attachment::Detached; break;
        case SettingsAction::CurveDown:
            config.detachedCurveDeg = clampCurveDeg(config.detachedCurveDeg - kDetachedCurveStepDeg);
            break;
        case SettingsAction::CurveUp:
            config.detachedCurveDeg = clampCurveDeg(config.detachedCurveDeg + kDetachedCurveStepDeg);
            break;
        case SettingsAction::PresetLeftBottom:
        case SettingsAction::PresetCenterBottom:
        case SettingsAction::PresetRightBottom:
        case SettingsAction::PresetLeftTop:
        case SettingsAction::PresetRightTop:
            for (const auto& preset : kPresets) {
                if (preset.action != action) continue;
                // 見える方向を保つため、今の距離に合わせて伸び縮みさせる
                const double scale = distanceScale(config);
                config.posX = roundMm(preset.x * scale);
                config.posY = roundMm(preset.y * scale);
                // 隅に置くと斜めから見ることになるので、置いた位置で面を頭に向ける
                faceHead(config);
            }
            break;
        case SettingsAction::PresetWrist:
            if (wrist) pose = WristPose();
            break;
        // 微調整。手首はパネル自身の軸（今の向きのパネルの右・上・面の向き）で 1cm ずつ。コントローラーの軸にすると、
        // 付けた手の向きによって、どちらへ動くのか分かりにくかった（実機で確かめた）
        case SettingsAction::MoveLeft:
            if (wrist) nudgeWristAlongPanel(pose, 0, -1); else config.posX = roundMm(config.posX - kNudgeM);
            break;
        case SettingsAction::MoveRight:
            if (wrist) nudgeWristAlongPanel(pose, 0, +1); else config.posX = roundMm(config.posX + kNudgeM);
            break;
        case SettingsAction::MoveUp:
            if (wrist) nudgeWristAlongPanel(pose, 1, +1); else config.posY = roundMm(config.posY + kNudgeM);
            break;
        case SettingsAction::MoveDown:
            if (wrist) nudgeWristAlongPanel(pose, 1, -1); else config.posY = roundMm(config.posY - kNudgeM);
            break;
        case SettingsAction::MoveNear:
        case SettingsAction::MoveFar: {
            const bool nearer = action == SettingsAction::MoveNear;
            if (wrist) {
                // 手首: 「近く」は面の向き（見ている人の方）へ、「遠く」はその逆へ
                nudgeWristAlongPanel(pose, 2, nearer ? +1 : -1);
                break;
            }
            // 頭: 距離だけ変えて、見える方向（x/z と y/z の比）は保つ
            const double oldZ = config.posZ;
            const double step = nearer ? kDepthStepM : -kDepthStepM;
            const double newZ = std::clamp(roundMm(oldZ + step), -3.0, -0.2);
            if (oldZ < 0) {
                config.posX = roundMm(config.posX * newZ / oldZ);
                config.posY = roundMm(config.posY * newZ / oldZ);
            }
            config.posZ = newZ;
            break;
        }
        // 向き（位置は変えない）。頭も手首も同じ約束（正で面が右・上・面を見て反時計回り）
        case SettingsAction::YawLeft:
            if (wrist) pose.yaw = stepAngle(pose.yaw, -1, 180.0, angleStepDeg);
            else config.yawDeg = stepAngle(config.yawDeg, -1, 180.0, angleStepDeg);
            break;
        case SettingsAction::YawRight:
            if (wrist) pose.yaw = stepAngle(pose.yaw, +1, 180.0, angleStepDeg);
            else config.yawDeg = stepAngle(config.yawDeg, +1, 180.0, angleStepDeg);
            break;
        case SettingsAction::PitchUp:
            if (wrist) pose.pitch = stepWristPitch(pose.pitch, +1, angleStepDeg);
            else config.pitchDeg = stepAngle(config.pitchDeg, +1, 90.0, angleStepDeg);
            break;
        case SettingsAction::PitchDown:
            if (wrist) pose.pitch = stepWristPitch(pose.pitch, -1, angleStepDeg);
            else config.pitchDeg = stepAngle(config.pitchDeg, -1, 90.0, angleStepDeg);
            break;
        // roll は正で、面を見て反時計回り（⟲）
        case SettingsAction::RollLeft:
            if (wrist) pose.roll = stepAngle(pose.roll, +1, 180.0, angleStepDeg);
            else config.rollDeg = stepAngle(config.rollDeg, +1, 180.0, angleStepDeg);
            break;
        case SettingsAction::RollRight:
            if (wrist) pose.roll = stepAngle(pose.roll, -1, 180.0, angleStepDeg);
            else config.rollDeg = stepAngle(config.rollDeg, -1, 180.0, angleStepDeg);
            break;
        case SettingsAction::FaceMe: faceHead(config); break;
        case SettingsAction::FaceForward:
            config.yawDeg = 0.0;
            config.pitchDeg = 0.0;
            config.rollDeg = 0.0;
            break;
        case SettingsAction::FadeOn: config.wristFade = true; break;
        case SettingsAction::FadeOff: config.wristFade = false; break;
        case SettingsAction::FadeAngleDown:
            config.wristFadeEndDeg = std::clamp(config.wristFadeEndDeg - kFadeAngleStepDeg, 35.0, 90.0);
            break;
        case SettingsAction::FadeAngleUp:
            config.wristFadeEndDeg = std::clamp(config.wristFadeEndDeg + kFadeAngleStepDeg, 35.0, 90.0);
            break;
        case SettingsAction::SizeDown: config.widthM = std::clamp(roundMm(config.widthM - 0.02), 0.06, 1.0); break;
        case SettingsAction::SizeUp: config.widthM = std::clamp(roundMm(config.widthM + 0.02), 0.06, 1.0); break;
        case SettingsAction::AlphaDown:
            config.alpha = std::clamp(std::round((config.alpha - 0.1) * 20.0) / 20.0, 0.2, 1.0);
            break;
        case SettingsAction::AlphaUp:
            config.alpha = std::clamp(std::round((config.alpha + 0.1) * 20.0) / 20.0, 0.2, 1.0);
            break;
        case SettingsAction::ClockOff: config.clockFormat = 0; break;
        case SettingsAction::Clock12: config.clockFormat = 12; break;
        case SettingsAction::Clock24: config.clockFormat = 24; break;
        case SettingsAction::Reset: resetDisplaySettings(config); break;
        case SettingsAction::LanguageJa: config.language = Language::Ja; break;
        case SettingsAction::LanguageEn: config.language = Language::En; break;
        case SettingsAction::AutostartOn:   // 自動起動は設定ファイルではなく systemd なので呼び出し側で扱う
        case SettingsAction::AutostartOff:
        case SettingsAction::Quit:  // 終了は呼び出し側で扱う
        case SettingsAction::UpdateCheckNow:    // 更新はどれも設定ファイルを変えない。呼び出し側で扱う
        case SettingsAction::UpdateInstall:
        case SettingsAction::UpdateConfirmYes:
        case SettingsAction::UpdateConfirmNo:
        case SettingsAction::UpdateRetry:
        case SettingsAction::UpdateDismiss:
        case SettingsAction::AngleStep1:  // 刻みは設定パネルの中だけで覚える（設定ファイルは変えない）
        case SettingsAction::AngleStep5:
        case SettingsAction::PageLayout:  // the page is the panel's own state
        case SettingsAction::PagePlacement:
        case SettingsAction::PageProfiles:  // profiles change the layout through the caller (LayoutAuthority)
        case SettingsAction::ProfileRow0:
        case SettingsAction::ProfileRow1:
        case SettingsAction::ProfileRow2:
        case SettingsAction::ProfileRow3:
        case SettingsAction::ProfileRow4:
        case SettingsAction::ProfileRow5:
        case SettingsAction::ProfileRow6:
        case SettingsAction::ProfileRow7:
        case SettingsAction::ProfileNew:
        case SettingsAction::ProfileUse:
        case SettingsAction::ProfileSave:
        case SettingsAction::ProfileRename:
        case SettingsAction::ProfileDelete:
        case SettingsAction::ProfileReset:
        case SettingsAction::BringInFront:  // needs the HMD pose: the caller does it
        case SettingsAction::None: break;
    }
    return !sameDisplaySettings(config, before);
}

SettingsPanel::SettingsPanel(const FontSet& fonts)
    : fonts_(fonts),
      editor_({kPad, kCardY, kEditorCardW, kCardH}, {kPaletteCardX, kCardY, kWidth - kPad - kPaletteCardX, kCardH}) {
    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
    cr_ = cairo_create(surface_);
    cairo_font_options_t* options = cairo_font_options_create();
    cairo_font_options_set_antialias(options, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options, CAIRO_HINT_STYLE_SLIGHT);
    cairo_set_font_options(cr_, options);
    cairo_font_options_destroy(options);
    layoutButtons();
}

SettingsPanel::~SettingsPanel() {
    cairo_destroy(cr_);
    cairo_surface_destroy(surface_);
}

void SettingsPanel::layoutButtons() {
    buttons_.clear();
    /**
     * ボタンを 1 つ足す。
     */
    const auto add = [this](SettingsAction action, double x, double y, double w, double h) {
        buttons_.push_back({action, x, y, w, h});
    };
    /**
     * セグメント切り替えの各部分を、それぞれボタンとして足す（押せる範囲はピル全体の高さを等分した幅）。
     */
    const auto segmented = [&add](double x, double y, double w, std::initializer_list<SettingsAction> actions) {
        const double part = w / static_cast<double>(actions.size());
        double left = x;
        for (const SettingsAction action : actions) {
            add(action, left, y, part, kButtonH);
            left += part;
        }
    };

    // The Layout page's cards are the editor's; only the header and the footer have buttons here
    if (mode_ == ButtonMode::Layout) return;
    if (mode_ == ButtonMode::Profiles) return;  // its rows and buttons are placed as they're drawn
    const bool wristLayout = mode_ == ButtonMode::Wrist;
    const bool detached = mode_ == ButtonMode::Detached;

    // 左のカード「パネル」: 表示・大きさ・透明度・時計・既定に戻す
    segmented(kRowControlX, rowY(0), kSegmentW, {SettingsAction::ShowOn, SettingsAction::ShowOff});
    add(SettingsAction::SizeDown, kRowControlX, rowY(1), kStepButtonW, kButtonH);
    add(SettingsAction::SizeUp, kRowControlX + kStepButtonW + kValueW, rowY(1), kStepButtonW, kButtonH);
    add(SettingsAction::AlphaDown, kRowControlX, rowY(2), kStepButtonW, kButtonH);
    add(SettingsAction::AlphaUp, kRowControlX + kStepButtonW + kValueW, rowY(2), kStepButtonW, kButtonH);
    segmented(kRowControlX, rowY(3), kControlW,
              {SettingsAction::ClockOff, SettingsAction::Clock12, SettingsAction::Clock24});
    add(SettingsAction::Reset, kRowControlX, rowY(4), kControlW, kButtonH);

    // 真ん中のカード「位置」: いちばん上に固定先
    segmented(kPosInnerX + kAttachLabelW, rowY(0), kPosInnerW - kAttachLabelW,
              {SettingsAction::AttachHead, SettingsAction::AttachLeft, SettingsAction::AttachRight,
               SettingsAction::AttachDetached});
    const double col[3] = {kPosInnerX, kPosInnerX + kPresetW + kGap, kPosInnerX + 2 * (kPresetW + kGap)};
    if (detached) {
        // Room: one wide "Bring it in front of me"; the row below is the laser hint
        add(SettingsAction::BringInFront, kPosInnerX, rowY(1), kPosInnerW, kButtonH);
    } else if (wristLayout) {
        // 手首: 位置のボタンは「手首の標準の位置」だけ（1 段ぶちぬき）
        add(SettingsAction::PresetWrist, kPosInnerX, rowY(1), kPosInnerW, kButtonH);
    } else {
        // 頭: 見える場所どおりに 3×2（上の段の真ん中は空き）
        add(SettingsAction::PresetLeftTop, col[0], rowY(1), kPresetW, kButtonH);
        add(SettingsAction::PresetRightTop, col[2], rowY(1), kPresetW, kButtonH);
        add(SettingsAction::PresetLeftBottom, col[0], rowY(2), kPresetW, kButtonH);
        add(SettingsAction::PresetCenterBottom, col[1], rowY(2), kPresetW, kButtonH);
        add(SettingsAction::PresetRightBottom, col[2], rowY(2), kPresetW, kButtonH);
    }
    // 微調整: 上下左右は十字キーの形、右に近く / 遠く（頭でも手首でも同じ場所）
    const double arrowCol[3] = {kPosInnerX, kPosInnerX + kArrowW + kGap, kPosInnerX + 2 * (kArrowW + kGap)};
    add(SettingsAction::MoveUp, arrowCol[1], rowY(3), kArrowW, kButtonH);
    add(SettingsAction::MoveLeft, arrowCol[0], rowY(4), kArrowW, kButtonH);
    add(SettingsAction::MoveDown, arrowCol[1], rowY(4), kArrowW, kButtonH);
    add(SettingsAction::MoveRight, arrowCol[2], rowY(4), kArrowW, kButtonH);
    add(SettingsAction::MoveNear, kDepthX, rowY(3), kDepthW, kButtonH);
    add(SettingsAction::MoveFar, kDepthX, rowY(4), kDepthW, kButtonH);

    // 右のカード「向き」: 位置の微調整と同じ十字（上の段に ⟲ ↑ ⟳、下の段に ← ↓ →）と、刻みの 1° / 5°
    const double facingCol[3] = {kFacingInnerX, kFacingInnerX + kFacingArrowW + kGap,
                                 kFacingInnerX + 2 * (kFacingArrowW + kGap)};
    add(SettingsAction::RollLeft, facingCol[0], rowY(0), kFacingArrowW, kButtonH);
    add(SettingsAction::PitchUp, facingCol[1], rowY(0), kFacingArrowW, kButtonH);
    add(SettingsAction::RollRight, facingCol[2], rowY(0), kFacingArrowW, kButtonH);
    add(SettingsAction::YawLeft, facingCol[0], rowY(1), kFacingArrowW, kButtonH);
    add(SettingsAction::PitchDown, facingCol[1], rowY(1), kFacingArrowW, kButtonH);
    add(SettingsAction::YawRight, facingCol[2], rowY(1), kFacingArrowW, kButtonH);
    segmented(kFacingInnerX, rowY(2), kFacingInnerW, {SettingsAction::AngleStep1, SettingsAction::AngleStep5});
    if (detached) {
        // Room: turn it to face the HMD (upright), and the curve (− value ＋, laid out like the wrist's fade angle)
        add(SettingsAction::FaceMe, kFacingInnerX, rowY(3), kFacingInnerW, kButtonH);
        const double right = kFacingInnerX + kFacingInnerW;
        add(SettingsAction::CurveDown, right - kControlW, rowY(4), kStepButtonW, kButtonH);
        add(SettingsAction::CurveUp, right - kStepButtonW, rowY(4), kStepButtonW, kButtonH);
    } else if (wristLayout) {
        // 手首: 傾けると消す（オン / オフ）と、消える角度（− 値 ＋）。見出しは左、ボタンは右詰め
        const double right = kFacingInnerX + kFacingInnerW;
        segmented(right - kSegmentW, rowY(3), kSegmentW, {SettingsAction::FadeOn, SettingsAction::FadeOff});
        add(SettingsAction::FadeAngleDown, right - kControlW, rowY(4), kStepButtonW, kButtonH);
        add(SettingsAction::FadeAngleUp, right - kStepButtonW, rowY(4), kStepButtonW, kButtonH);
    } else {
        // 頭: 自分に向ける / 正面向き
        const double faceW = (kFacingInnerW - kGap) / 2;
        add(SettingsAction::FaceMe, kFacingInnerX, rowY(3), faceW, kButtonH);
        add(SettingsAction::FaceForward, kFacingInnerX + faceW + kGap, rowY(3), faceW, kButtonH);
    }
    // 下の段の言語・自動起動・終了と更新の帯は、文言の幅が言語や状態で変わるので render() のたびに置き直す
}

std::string SettingsPanel::labelOf(SettingsAction action, const UiText& text) const {
    switch (action) {
        case SettingsAction::ShowOn: return text.on;
        case SettingsAction::ShowOff: return text.off;
        case SettingsAction::LanguageJa: return "日本語";
        case SettingsAction::LanguageEn: return "English";
        case SettingsAction::AttachHead: return text.attachHead;
        case SettingsAction::AttachLeft: return text.attachLeft;
        case SettingsAction::AttachRight: return text.attachRight;
        case SettingsAction::AttachDetached: return text.attachRoom;
        case SettingsAction::BringInFront: return text.bringInFront;
        case SettingsAction::PresetLeftBottom: return text.bottomLeft;
        case SettingsAction::PresetCenterBottom: return text.bottomCenter;
        case SettingsAction::PresetRightBottom: return text.bottomRight;
        case SettingsAction::PresetLeftTop: return text.topLeft;
        case SettingsAction::PresetRightTop: return text.topRight;
        case SettingsAction::PresetWrist: return text.wristPreset;
        case SettingsAction::MoveLeft: return text.moveLeft;
        case SettingsAction::MoveRight: return text.moveRight;
        case SettingsAction::MoveUp: return text.moveUp;
        case SettingsAction::MoveDown: return text.moveDown;
        case SettingsAction::MoveNear: return text.nearer;
        case SettingsAction::MoveFar: return text.farther;
        case SettingsAction::YawLeft: return text.yawLeft;
        case SettingsAction::YawRight: return text.yawRight;
        case SettingsAction::PitchUp: return text.pitchUp;
        case SettingsAction::PitchDown: return text.pitchDown;
        case SettingsAction::RollLeft: return text.rollLeft;
        case SettingsAction::RollRight: return text.rollRight;
        case SettingsAction::FaceMe: return text.faceMe;
        case SettingsAction::FaceForward: return text.faceForward;
        case SettingsAction::AngleStep1: return text.angleStep1;
        case SettingsAction::AngleStep5: return text.angleStep5;
        case SettingsAction::FadeOn: return text.on;
        case SettingsAction::FadeOff: return text.off;
        case SettingsAction::SizeDown:
        case SettingsAction::AlphaDown:
        case SettingsAction::FadeAngleDown:
        case SettingsAction::CurveDown: return "−";
        case SettingsAction::SizeUp:
        case SettingsAction::AlphaUp:
        case SettingsAction::FadeAngleUp:
        case SettingsAction::CurveUp: return "＋";
        case SettingsAction::ClockOff: return text.off;
        case SettingsAction::Clock12: return text.clock12;
        case SettingsAction::Clock24: return text.clock24;
        case SettingsAction::Reset: return text.reset;
        case SettingsAction::Quit: return quitArmed_ ? text.quitConfirm : text.quit;
        case SettingsAction::AutostartOn: return text.on;
        case SettingsAction::AutostartOff: return text.off;
        case SettingsAction::UpdateCheckNow: return text.updateCheckNow;
        case SettingsAction::UpdateInstall: return text.updateButton;
        case SettingsAction::UpdateConfirmYes: return text.updateConfirmYes;
        case SettingsAction::UpdateConfirmNo: return text.updateConfirmNo;
        case SettingsAction::UpdateRetry: return text.updateRetry;
        case SettingsAction::UpdateDismiss: return text.updateDismiss;
        case SettingsAction::PageLayout: return text.pageLayout;
        case SettingsAction::PagePlacement: return text.pagePlacement;
        case SettingsAction::PageProfiles: return text.pageProfiles;
        case SettingsAction::ProfileNew: return text.profileNew;
        case SettingsAction::ProfileSave: return text.profileSave;
        case SettingsAction::ProfileRename: return text.profileRename;
        case SettingsAction::ProfileUse:
        case SettingsAction::ProfileReset: {
            if (profileArmed_ != action) return action == SettingsAction::ProfileUse ? text.profileUse : text.profileReset;
            const std::string again = text.profileTapAgain;  // "Tap again: ..." -> "Tap again" on the button
            return again.substr(0, again.find(':'));
        }
        case SettingsAction::ProfileDelete:
            return profileArmed_ == action ? text.profileDeleteAgain : text.profileDelete;
        case SettingsAction::ProfileRow0:  // the rows draw their own names
        case SettingsAction::ProfileRow1:
        case SettingsAction::ProfileRow2:
        case SettingsAction::ProfileRow3:
        case SettingsAction::ProfileRow4:
        case SettingsAction::ProfileRow5:
        case SettingsAction::ProfileRow6:
        case SettingsAction::ProfileRow7: return "";
        case SettingsAction::None: break;
    }
    return "";
}

SettingsAction SettingsPanel::hitTest(double x, double y) const {
    for (const auto& b : buttons_) {
        const bool isAutostart = b.action == SettingsAction::AutostartOn || b.action == SettingsAction::AutostartOff;
        if (isAutostart && !autostartInstalled_) continue;  // ユニットが無いときは押せない
        // A Profiles page button drawn disabled (nothing selected, all 8 used, no active profile)
        if (std::find(profileDisabled_.begin(), profileDisabled_.end(), b.action) != profileDisabled_.end()) continue;
        if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h) return b.action;
    }
    return SettingsAction::None;
}

bool SettingsPanel::pointerMove(double x, double y) {
    // The editor gets every move on its page (a drag may run over the header and the footer)
    const bool editor = page_ == SettingsPage::Layout && editor_.pointerMove(x, y);
    const SettingsAction now = editor_.busy() ? SettingsAction::None : hitTest(x, y);
    if (now == hover_) return editor;
    hover_ = now;
    return true;
}

bool SettingsPanel::scroll(double dy) {
    return page_ == SettingsPage::Layout && editor_.scroll(dy);
}

SettingsAction SettingsPanel::pointerDown(double x, double y, double now) {
    hover_ = hitTest(x, y);
    // Any press but the armed profile button cancels the pending second tap (before the early returns below)
    if (hover_ != profileArmed_) profileArmed_ = SettingsAction::None;
    // Off the header / footer buttons, the Layout page's press goes to the editor
    if (hover_ == SettingsAction::None && page_ == SettingsPage::Layout) {
        quitArmed_ = false;
        updateConfirmArmed_ = false;
        editor_.pointerDown(x, y);
        return SettingsAction::None;
    }
    pressed_ = hover_;
    if (pressed_ == SettingsAction::PageLayout || pressed_ == SettingsAction::PagePlacement ||
        pressed_ == SettingsAction::PageProfiles) {
        page_ = pressed_ == SettingsAction::PageLayout      ? SettingsPage::Layout
                : pressed_ == SettingsAction::PagePlacement ? SettingsPage::Placement
                                                            : SettingsPage::Profiles;
        editor_.pointerLeave();
        quitArmed_ = false;
        updateConfirmArmed_ = false;
        profileArmed_ = SettingsAction::None;
        return SettingsAction::None;
    }
    if (pressed_ == SettingsAction::Quit) {
        // 誤って押しても終わらないよう、1 回目は確認の表示にするだけ
        if (quitArmed_ && now <= quitArmedUntil_) return SettingsAction::Quit;
        quitArmed_ = true;
        quitArmedUntil_ = now + kQuitConfirmSec;
        return SettingsAction::None;
    }
    quitArmed_ = false;  // 別のボタンを押したら確認は取り消す
    if (pressed_ == SettingsAction::UpdateInstall) {
        // 「更新する」の 1 回目は、確認の表示（「%s に更新しますか？」＋ 更新する / やめる）に切り替えるだけ
        updateConfirmArmed_ = true;
        return SettingsAction::None;
    }
    if (pressed_ == SettingsAction::UpdateConfirmNo) {
        updateConfirmArmed_ = false;  // 確認をやめる
        return SettingsAction::None;
    }
    // ここまで来たら確認の表示は終わり（Yes で実行するときも、ほかのボタンを押して取り消すときも）
    updateConfirmArmed_ = false;
    // Profiles page. A row selects; Delete always, and Use / Reset when the live layout would be lost, need a
    // second tap within kQuitConfirmSec. Any other press cancels a pending one
    if (pressed_ >= SettingsAction::ProfileRow0 && pressed_ <= SettingsAction::ProfileRow7) {
        const size_t row = static_cast<size_t>(pressed_) - static_cast<size_t>(SettingsAction::ProfileRow0);
        if (row < profileRowIds_.size()) selectedProfile_ = profileRowIds_[row];
        profileArmed_ = SettingsAction::None;
        return SettingsAction::None;
    }
    if (pressed_ == SettingsAction::ProfileUse || pressed_ == SettingsAction::ProfileReset ||
        pressed_ == SettingsAction::ProfileDelete) {
        const bool twoTaps = pressed_ == SettingsAction::ProfileDelete || lastProfiles_.unsaved;
        if (twoTaps && !(profileArmed_ == pressed_ && now <= profileArmedUntil_)) {
            profileArmed_ = pressed_;
            profileArmedUntil_ = now + kQuitConfirmSec;
            return SettingsAction::None;
        }
    }
    profileArmed_ = SettingsAction::None;
    if (pressed_ == SettingsAction::AngleStep1 || pressed_ == SettingsAction::AngleStep5) {
        // 刻みはこのパネルの中だけで覚える（設定ファイルには保存しない）
        angleStepDeg_ = pressed_ == SettingsAction::AngleStep5 ? 5.0 : 1.0;
        return SettingsAction::None;
    }
    return pressed_;
}

bool SettingsPanel::pointerUp(double x, double y) {
    const bool editor = editor_.pointerUp(x, y);  // A drop or a tap in the editor
    if (pressed_ == SettingsAction::None) return editor;
    pressed_ = SettingsAction::None;
    return true;
}

bool SettingsPanel::pointerLeave() {
    // The laser left the tab (or the dashboard closed) mid-drag: cancel it, commit nothing
    const bool changed = editor_.pointerLeave() || hover_ != SettingsAction::None || pressed_ != SettingsAction::None;
    hover_ = SettingsAction::None;
    pressed_ = SettingsAction::None;
    return changed;
}

bool SettingsPanel::tick(double now) {
    bool changed = false;
    if (quitArmed_ && now > quitArmedUntil_) {
        quitArmed_ = false;
        changed = true;
    }
    if (profileArmed_ != SettingsAction::None && now > profileArmedUntil_) {
        profileArmed_ = SettingsAction::None;
        changed = true;
    }
    return changed;
}

void SettingsPanel::armQuitForPreview() {
    quitArmed_ = true;
    quitArmedUntil_ = 1e300;
}

void SettingsPanel::armUpdateConfirmForPreview() {
    updateConfirmArmed_ = true;
}

const SettingsPanel::Button* SettingsPanel::findButton(SettingsAction action) const {
    for (const auto& b : buttons_) {
        if (b.action == action) return &b;
    }
    return nullptr;
}

void SettingsPanel::placeFooterButton(SettingsAction action, double x, double y, double w, double h) {
    for (auto& b : buttons_) {
        if (b.action == action) {
            b = {action, x, y, w, h};
            return;
        }
    }
    buttons_.push_back({action, x, y, w, h});
}

void SettingsPanel::drawSegmented(const Pen& pen, const UiText& text, std::initializer_list<SettingsAction> actions,
                                  int selected, bool usable) const {
    std::vector<const Button*> parts;
    for (const SettingsAction action : actions) {
        const Button* b = findButton(action);
        if (b == nullptr) return;
        parts.push_back(b);
    }
    if (parts.empty()) return;
    // Drawn by the shared pill (core/render/draw.h), the one the layout editors use too
    std::vector<PillPart> pill;
    for (const Button* b : parts) pill.push_back({labelOf(b->action, text), pressed_ == b->action, hover_ == b->action});
    const double x = parts.front()->x;
    drawSegmentedPill(pen, x, parts.front()->y, parts.back()->x + parts.back()->w - x, parts.front()->h, pill, selected,
                      usable);
}

void SettingsPanel::drawButton(const Pen& pen, const UiText& text, SettingsAction action, bool selected,
                               double size, int rotateIcon, bool check) const {
    const Button* b = findButton(action);
    if (b == nullptr) return;
    const bool pressed = pressed_ == action;
    const bool hovered = hover_ == action;
    const double r = 18;
    if (selected) {
        // 選ばれている位置: アクセントの塗り ＋ ✓ ＋ 太字
        pen.color(pressed ? kAccentPressed : kAccent);
        pen.roundedRect(b->x, b->y, b->w, b->h, r);
        cairo_fill(pen.cr);
    } else {
        pen.color(pressed || hovered ? kControlHover : kControl);
        pen.roundedRect(b->x, b->y, b->w, b->h, r);
        cairo_fill(pen.cr);
        // 押している間は枠をアクセントにして、押したことが分かるようにする
        strokeRounded(pen, b->x, b->y, b->w, b->h, r, pressed ? kAccent : kBorder, 2);
    }
    const std::string label = labelOf(action, text);
    const double checkW = selected && check ? size * 0.9 : 0;
    const double iconW = rotateIcon != 0 ? size * 1.15 : 0;  // 回す向きの絵と、文言との間
    const double labelSize = fitSize(pen, label, size, size * 0.65, b->w - 20 - checkW - iconW, true);
    const double labelW = pen.measure(label, labelSize, true);
    const double tx = b->x + (b->w - labelW - checkW - iconW) / 2;
    const Color fg = selected ? kOnAccent : kText;
    if (selected && check) drawCheck(pen.cr, tx + checkW * 0.4, b->y + b->h / 2, size * 0.72, kOnAccent);
    const double labelX = tx + checkW + (rotateIcon < 0 ? iconW : 0);
    if (rotateIcon != 0) {
        const double iconCx = rotateIcon < 0 ? tx + checkW + size * 0.45 : labelX + labelW + size * 0.7;
        drawRotateArrow(pen.cr, iconCx, b->y + b->h / 2, size * 0.4, rotateIcon < 0, fg);
    }
    pen.text(labelX, centerBaseline(b->y, b->h, labelSize), label, labelSize, fg, true);
}

double SettingsPanel::drawHeader(const Pen& pen, const UiText& text, const Config& config) const {
    pen.text(kPad, centerBaseline(kHeaderY, kHeaderH, 34), text.settingsTitle, 34, kText, true);
    // 右上の状態のピル: 表示中は緑の塗り＋●＋文字、非表示は灰色の塗り＋○＋文字（色だけで伝えない）
    const std::string label = config.visible ? text.panelShown : text.panelHidden;
    const double size = 20;
    const double h = 42;
    const double y = kHeaderY + (kHeaderH - h) / 2;
    const double w = pen.measure(label, size, true) + 58;
    const double x = kWidth - kPad - w;
    pen.color(config.visible ? kSuccessTint : kControl);
    pen.roundedRect(x, y, w, h, h / 2);
    cairo_fill(pen.cr);
    const Color c = config.visible ? kSuccess : kTextMuted;
    if (config.visible) {
        drawDisc(pen.cr, x + 24, y + h / 2, 6.5, c);
    } else {
        pen.color(c);
        cairo_set_line_width(pen.cr, 2.5);
        cairo_new_sub_path(pen.cr);
        cairo_arc(pen.cr, x + 24, y + h / 2, 5.5, 0, 2 * M_PI);
        cairo_stroke(pen.cr);
    }
    pen.text(x + 40, centerBaseline(y, h, size), label, size, c, true);
    return x;
}

void SettingsPanel::drawPanelCard(const Pen& pen, const UiText& text, const Config& config) const {
    drawCard(pen, kLeftCardX, kCardY, kLeftCardW, kCardH, 20, kCard, kCard, 0);
    pen.text(kLeftCardX + kCardPad, kCardY + 42, text.cardPanel, 24, kText, true);
    // 行の見出し（左）とボタン（右）
    const char* labels[4] = {text.rowShow, text.rowSize, text.rowOpacity, text.rowClock};
    for (int i = 0; i < 4; ++i) {
        const double size = fitSize(pen, labels[i], 24, 16, kRowControlX - kLeftCardX - kCardPad - 12, false);
        pen.text(kLeftCardX + kCardPad, centerBaseline(rowY(i), kButtonH, size), labels[i], size, kTextMuted);
    }
    drawSegmented(pen, text, {SettingsAction::ShowOn, SettingsAction::ShowOff}, config.visible ? 0 : 1, true);

    // 大きさ・透明度: − 値 ＋
    char value[32];
    const double valueCx = kRowControlX + kStepButtonW + kValueW / 2;
    std::snprintf(value, sizeof(value), "%.0f cm", config.widthM * 100.0);
    textCentered(pen, valueCx, centerBaseline(rowY(1), kButtonH, 28), value, 28, kText, true);
    std::snprintf(value, sizeof(value), "%.0f%%", config.alpha * 100.0);
    textCentered(pen, valueCx, centerBaseline(rowY(2), kButtonH, 28), value, 28, kText, true);
    for (const SettingsAction action :
         {SettingsAction::SizeDown, SettingsAction::SizeUp, SettingsAction::AlphaDown, SettingsAction::AlphaUp}) {
        drawButton(pen, text, action, false, 30);
    }
    // 時計: オフ / 12h / 24h
    const int clock = config.clockFormat == 12 ? 1 : (config.clockFormat == 24 ? 2 : 0);
    drawSegmented(pen, text, {SettingsAction::ClockOff, SettingsAction::Clock12, SettingsAction::Clock24}, clock, true);
    drawButton(pen, text, SettingsAction::Reset, false, 24);
}

void SettingsPanel::drawPositionCard(const Pen& pen, const UiText& text, const Config& config) const {
    drawCard(pen, kPosCardX, kCardY, kPosCardW, kCardH, 20, kCard, kCard, 0);
    const double titleW = pen.text(kPosInnerX, kCardY + 42, text.rowPosition, 24, kText, true);
    // いまの位置（cm）を見出しの右に。頭は HMD から見て（前は前方への距離）。手首は標準の位置からの距離だけ
    // （ボタンはパネル自身の軸で動かすので、コントローラーの座標の数字はボタンと合わず、パネルの軸の数字は向きを
    // 変えるだけで変わってしまう。距離なら向きによらず、どれだけ動かしたかが分かる）
    std::string where;
    if (mode_ == ButtonMode::Detached) {
        // Room: the standing-space position (the origin is SteamVR's, on the floor)
        const DetachedPose& room = config.detachedPose;
        where = room.valid ? std::string(text.positionNow) + "  " + text.posX + " " + signedCm(room.m[0][3]) + "  " +
                                 text.posY + " " + signedCm(room.m[1][3]) + "  " + text.posZ + " " +
                                 signedCm(room.m[2][3]) + " cm"
                           : std::string(text.roomNotPlaced);
    } else if (mode_ == ButtonMode::Wrist) {
        char distance[96];
        std::snprintf(distance, sizeof(distance), text.wristOffsetFormat,
                      std::round(wristDistanceFromStandard(selectedWrist(config)) * 100.0) + 0.0);
        where = std::string(text.positionNow) + "  " + distance;
    } else {
        where = std::string(text.positionNow) + "  " + text.posX + " " + signedCm(config.posX) + "  " + text.posY +
                " " + signedCm(config.posY) + "  " + text.posZ + " " + signedCm(-config.posZ) + " cm";
    }
    const double whereSize = fitSize(pen, where, 18, 13, kPosInnerW - titleW - 16, false);
    pen.text(kPosInnerX + kPosInnerW, kCardY + 40, where, whereSize, kTextMuted, false, true);

    // 固定先: 見出し（左）と 3 択（右）
    const double attachSize = fitSize(pen, text.rowAttachment, 24, 16, kAttachLabelW - 12, false);
    pen.text(kPosInnerX, centerBaseline(rowY(0), kButtonH, attachSize), text.rowAttachment, attachSize, kTextMuted);
    const int attach = config.attachment == Attachment::LeftWrist    ? 1
                       : config.attachment == Attachment::RightWrist ? 2
                       : config.attachment == Attachment::Detached   ? 3
                                                                     : 0;
    drawSegmented(pen, text,
                  {SettingsAction::AttachHead, SettingsAction::AttachLeft, SettingsAction::AttachRight,
                   SettingsAction::AttachDetached},
                  attach, true);

    // 位置のボタン（今その位置なら ✓）
    if (mode_ == ButtonMode::Detached) {
        drawButton(pen, text, SettingsAction::BringInFront, false, 24);
        const double hintSize = fitSize(pen, text.roomHint, 20, 13, kPosInnerW, false);
        pen.text(kPosInnerX, centerBaseline(rowY(2), kButtonH, hintSize), text.roomHint, hintSize, kTextMuted);
    } else if (mode_ == ButtonMode::Wrist) {
        drawButton(pen, text, SettingsAction::PresetWrist, matchesWristPreset(selectedWrist(config)), 24);
    } else {
        for (const auto& preset : kPresets) drawButton(pen, text, preset.action, matchesPreset(preset, config), 24);
    }
    // 微調整の見出しは、十字の左上の空いたところに
    const double nudgeSize = fitSize(pen, text.rowNudge, 20, 14, kArrowW, false);
    pen.text(kPosInnerX, centerBaseline(rowY(3), kButtonH, nudgeSize), text.rowNudge, nudgeSize, kTextMuted);
    for (const SettingsAction action : {SettingsAction::MoveUp, SettingsAction::MoveLeft, SettingsAction::MoveDown,
                                        SettingsAction::MoveRight, SettingsAction::MoveNear, SettingsAction::MoveFar}) {
        drawButton(pen, text, action, false, 24);
    }
}

void SettingsPanel::drawFacingCard(const Pen& pen, const UiText& text, const Config& config) const {
    drawCard(pen, kFacingCardX, kCardY, kFacingCardW, kCardH, 20, kCard, kCard, 0);
    const double titleW = pen.text(kFacingInnerX, kCardY + 42, text.cardFacing, 24, kText, true);
    // いまの向き（度）を見出しの右に（見出しと重ならない幅で）。手首のときは、その手首の値
    double yaw = config.yawDeg;
    double pitch = config.pitchDeg;
    double roll = config.rollDeg;
    if (mode_ == ButtonMode::Wrist) {
        const WristPose& pose = selectedWrist(config);
        yaw = pose.yaw;
        pitch = pose.pitch;
        roll = pose.roll;
    }
    const std::string now = std::string(text.facingNow) + "  " + text.yawName + " " + signedDegrees(yaw) + "  " +
                            text.pitchName + " " + signedDegrees(pitch) + "  " + text.rollName + " " +
                            signedDegrees(roll);
    // Room: a room pose has no yaw / pitch / roll of its own (the buttons turn it about its own axes)
    if (mode_ != ButtonMode::Detached) {
        const double nowSize = fitSize(pen, now, 18, 12, kFacingInnerW - titleW - 16, false);
        pen.text(kFacingInnerX + kFacingInnerW, kCardY + 40, now, nowSize, kTextMuted, false, true);
    }

    for (const SettingsAction action :
         {SettingsAction::YawLeft, SettingsAction::YawRight, SettingsAction::PitchUp, SettingsAction::PitchDown}) {
        drawButton(pen, text, action, false, 24);
    }
    drawButton(pen, text, SettingsAction::RollLeft, false, 24, -1);
    drawButton(pen, text, SettingsAction::RollRight, false, 24, +1);
    drawSegmented(pen, text, {SettingsAction::AngleStep1, SettingsAction::AngleStep5}, angleStepDeg_ == 5.0 ? 1 : 0,
                  true);
    if (mode_ == ButtonMode::Detached) {
        drawButton(pen, text, SettingsAction::FaceMe, false, 24);
        // Room: Curve  − 40° ＋  (the arc the whole panel wraps)
        const double right = kFacingInnerX + kFacingInnerW;
        const double labelSize = fitSize(pen, text.rowCurve, 24, 16, kFacingInnerW - kControlW - 12, false);
        pen.text(kFacingInnerX, centerBaseline(rowY(4), kButtonH, labelSize), text.rowCurve, labelSize, kTextMuted);
        char value[16];
        std::snprintf(value, sizeof(value), "%d°", clampCurveDeg(config.detachedCurveDeg));
        const double valueSize = fitSize(pen, value, 28, 18, kValueW - 20, true);
        textCentered(pen, right - kStepButtonW - kValueW / 2, centerBaseline(rowY(4), kButtonH, valueSize), value,
                     valueSize, kText, true);
        drawButton(pen, text, SettingsAction::CurveDown, false, 30);
        drawButton(pen, text, SettingsAction::CurveUp, false, 30);
    } else if (mode_ == ButtonMode::Wrist) {
        // 傾けると消す（オン / オフ）と、消える角度（薄くなり始める角度〜消えきる角度）
        const double right = kFacingInnerX + kFacingInnerW;
        const char* labels[2] = {text.rowWristFade, text.rowFadeAngle};
        const double labelW[2] = {kFacingInnerW - kSegmentW - 12, kFacingInnerW - kControlW - 12};
        for (int i = 0; i < 2; ++i) {
            const double size = fitSize(pen, labels[i], 24, 16, labelW[i], false);
            pen.text(kFacingInnerX, centerBaseline(rowY(3 + i), kButtonH, size), labels[i], size, kTextMuted);
        }
        drawSegmented(pen, text, {SettingsAction::FadeOn, SettingsAction::FadeOff}, config.wristFade ? 0 : 1, true);
        char value[48];
        std::snprintf(value, sizeof(value), text.fadeRangeFormat, config.wristFadeEndDeg - 30.0, config.wristFadeEndDeg);
        const double valueSize = fitSize(pen, value, 28, 18, kValueW - 20, true);
        textCentered(pen, right - kStepButtonW - kValueW / 2, centerBaseline(rowY(4), kButtonH, valueSize), value,
                     valueSize, kText, true);
        drawButton(pen, text, SettingsAction::FadeAngleDown, false, 30);
        drawButton(pen, text, SettingsAction::FadeAngleUp, false, 30);
    } else {
        // 今その向きになっていれば ✓（位置のプリセットと同じ見せ方）
        drawButton(pen, text, SettingsAction::FaceMe, isFacingHead(config), 24);
        drawButton(pen, text, SettingsAction::FaceForward, isFacingForward(config), 24);
    }
}

void SettingsPanel::drawFooter(const Pen& pen, const UiText& text, const Config& config,
                               const AutostartStatus& autostart) {
    const double y = kFooterY;
    const double labelSize = 20;
    double x = kPad;
    // 言語
    x += pen.text(x, centerBaseline(y, kButtonH, labelSize), text.rowLanguage, labelSize, kTextMuted) + 14;
    placeFooterButton(SettingsAction::LanguageJa, x, y, kFooterSegmentW / 2, kButtonH);
    placeFooterButton(SettingsAction::LanguageEn, x + kFooterSegmentW / 2, y, kFooterSegmentW / 2, kButtonH);
    drawSegmented(pen, text, {SettingsAction::LanguageJa, SettingsAction::LanguageEn},
                  config.language == Language::Ja ? 0 : 1, true);
    x += kFooterSegmentW + 40;
    // 自動起動（ユニットが無いときはグレーで押せない。切り替え中も薄く）
    const bool installed = autostart.state != AutostartStatus::State::NotInstalled;
    autostartInstalled_ = installed;
    x += pen.text(x, centerBaseline(y, kButtonH, labelSize), text.rowAutostart, labelSize, kTextMuted) + 14;
    placeFooterButton(SettingsAction::AutostartOn, x, y, kFooterSegmentW / 2, kButtonH);
    placeFooterButton(SettingsAction::AutostartOff, x + kFooterSegmentW / 2, y, kFooterSegmentW / 2, kButtonH);
    const int selected = autostart.state == AutostartStatus::State::Enabled
                             ? 0
                             : (autostart.state == AutostartStatus::State::Disabled ? 1 : -1);
    drawSegmented(pen, text, {SettingsAction::AutostartOn, SettingsAction::AutostartOff}, selected,
                  installed && !autostart.busy);

    // 終了（赤い枠の控えめなボタン。確認中は赤い塗り）
    const double quitX = kWidth - kPad - kQuitW;
    placeFooterButton(SettingsAction::Quit, quitX, y, kQuitW, kButtonH);
    const bool quitHover = hover_ == SettingsAction::Quit || pressed_ == SettingsAction::Quit;
    pen.color(quitArmed_ ? kDanger : (quitHover ? kControlHover : kQuitFill));
    pen.roundedRect(quitX, y, kQuitW, kButtonH, kButtonH / 2);
    cairo_fill(pen.cr);
    strokeRounded(pen, quitX, y, kQuitW, kButtonH, kButtonH / 2, kDanger, 2);
    const std::string quitLabel = labelOf(SettingsAction::Quit, text);
    const double quitSize = fitSize(pen, quitLabel, 24, 14, kQuitW - 28, true);
    textCentered(pen, quitX + kQuitW / 2, centerBaseline(y, kButtonH, quitSize), quitLabel, quitSize,
                 quitArmed_ ? kOnAccent : kText, true);

    // 最下行（1 行だけ）: 失敗（赤）> 切り替え中 > ユニットが無い > ふだんの説明
    const double lineY = kHeight - 24;
    const double maxW = kWidth - kPad * 2;
    std::string note;
    Color noteColor = kTextMuted;
    bool bold = false;
    if (autostart.failed) {
        note = text.autostartFailed;
        noteColor = kDanger;
        bold = true;
    } else if (autostart.busy) {
        note = text.autostartBusy;
        noteColor = kText;
    } else if (!installed) {
        note = text.autostartNotInstalled;
    } else {
        note = std::string(text.footer) + text.sentenceBreak + text.autostartNextLaunch;
    }
    pen.text(kPad, lineY, note, fitSize(pen, note, 17, 12, maxW, bold), noteColor, bold);
}

void SettingsPanel::drawUpdateBar(const Pen& pen, const UiText& text, const Config& config,
                                   const frame_updater::UpdateStatus& update, double left, double right) {
    using frame_updater::UpdateState;

    // 前フレームで置いたボタンを消してから、今の状態で要るものだけ置き直す（同じ場所に別のボタンが
    // 重なって残らないように）
    for (const SettingsAction a :
         {SettingsAction::UpdateCheckNow, SettingsAction::UpdateInstall, SettingsAction::UpdateConfirmYes,
          SettingsAction::UpdateConfirmNo, SettingsAction::UpdateRetry, SettingsAction::UpdateDismiss}) {
        buttons_.erase(std::remove_if(buttons_.begin(), buttons_.end(), [a](const Button& b) { return b.action == a; }),
                      buttons_.end());
    }

    const bool confirming = updateConfirmArmed_ && update.state == UpdateState::Available;

    /** printf 書式（%s が 1 つ）に版を当てはめる。 */
    const auto format1 = [](const char* fmt, const std::string& value) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), fmt, value.c_str());
        return std::string(buf);
    };

    std::string headline;
    std::string detail;     // 見出しの後ろに続ける補足（入りきらなければ 2 行目に回す）
    std::string detailSep;  // 1 行で出すときの、見出しと補足の間
    Color color = kTextMuted;
    bool bold = false;
    std::vector<SettingsAction> buttons;  // 右に置くボタン（左から順）
    // 帯の枠（Frame のアプリ共通の見本に合わせる）: 新しい版・確認はピンク、失敗は赤、ほかは枠なし
    Color border = kCard;
    double borderWidth = 0;
    std::string hint;  // 右に小さく出す灰色の補足（更新中だけ）

    if (confirming) {
        headline = format1(text.updateConfirmFormat, update.latest);
        color = kText;
        bold = true;
        border = kAccent;
        borderWidth = 2;
        buttons = {SettingsAction::UpdateConfirmNo, SettingsAction::UpdateConfirmYes};
    } else {
        switch (update.state) {
            case UpdateState::Unknown:
                headline = update.checking ? text.updateChecking : update.current;
                if (!update.checking) buttons = {SettingsAction::UpdateCheckNow};
                break;
            case UpdateState::UpToDate:
                headline = format1(text.updateUpToDateFormat, update.current);
                if (!update.checking) buttons = {SettingsAction::UpdateCheckNow};
                break;
            case UpdateState::Available:
                headline = format1(text.updateAvailableFormat, update.latest);
                color = kAccentText;
                bold = true;
                border = kAccent;
                borderWidth = 2;
                if (!update.installable) {
                    detail = text.updateManual;
                    detailSep = "  ";
                }
                // 見本どおり［今すぐ確かめる］［更新する］の順（強調の［更新する］を右端に）
                if (!update.checking) buttons.push_back(SettingsAction::UpdateCheckNow);
                if (update.installable) buttons.push_back(SettingsAction::UpdateInstall);
                break;
            case UpdateState::Installing:
                headline = format1(text.updateInstallingFormat, updateStepText(config.language, update.step));
                color = kText;
                hint = text.updateConfirmHint;  // 途中で画面が閉じて開き直すことがある旨
                break;  // 進行中はボタンなし
            case UpdateState::Installed:
                headline = format1(text.updateInstalledFormat, update.version);
                color = kSuccess;
                bold = true;
                buttons = {SettingsAction::UpdateDismiss};
                break;
            case UpdateState::CheckFailed:
                // くわしい理由（updateLogHint）はここには出さない。ボタンと並ぶと長い文言で入りきらないため
                // （journalctl・README の「うまく動かないとき」を参照）
                headline = text.updateCheckFailed;
                detail = updateErrorText(config.language, update.error);
                detailSep = " ";
                // 枠は普通のまま、文字だけ赤（オフラインだと毎日出るので、赤い枠は目立ちすぎる）
                color = kDanger;
                bold = true;
                if (!update.checking) buttons = {SettingsAction::UpdateCheckNow};
                break;
            case UpdateState::InstallFailed:
                headline = text.updateInstallFailed;
                detail = updateErrorText(config.language, update.error);
                detailSep = " ";
                color = kDanger;
                bold = true;
                border = kDanger;
                borderWidth = 2;
                buttons = {SettingsAction::UpdateRetry, SettingsAction::UpdateDismiss};
                break;
        }
    }

    drawCard(pen, left, kHeaderY, right - left, kHeaderH, 16, kCard, border, borderWidth);

    // 右のボタンを右詰めで置く（幅は文言に合わせる。高さはほかのボタンと同じ 68px）
    const double btnY = kHeaderY + (kHeaderH - kButtonH) / 2;
    const double gap = 12;
    const double inset = 8;  // 帯の端とボタンの間（上下と同じ）
    double buttonX = right - inset;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const double w = pen.measure(labelOf(*it, text), 22, true) + 40;
        buttonX -= w;
        placeFooterButton(*it, buttonX, btnY, w, kButtonH);
        buttonX -= gap;
    }
    const double textLeft = left + kCardPad;
    double textRight = buttons.empty() ? right - kCardPad : buttonX + gap - 16;
    if (!hint.empty()) {
        // 補足は右詰めで小さく。見出しの幅を先に取り、残りに入れる。1 行で 15px に届かなければ、
        // 文の切れ目（「。」/ ". "）で 2 行に分ける（文言は変えない）
        const double headW = pen.measure(headline, 22, bold);
        const double hintRight = right - kCardPad;
        const double hintMaxW = hintRight - (textLeft + headW + 24);
        const double hintSize = fitSize(pen, hint, 17, 12, hintMaxW, false);
        size_t cut = hint.find("。");
        size_t cutLen = 3;  // 「。」は UTF-8 で 3 バイト
        if (cut == std::string::npos) {
            cut = hint.find(". ");
            cutLen = 1;
        }
        if (hintSize >= 15 || cut == std::string::npos) {
            pen.text(hintRight, centerBaseline(kHeaderY, kHeaderH, hintSize), hint, hintSize, kTextMuted, false, true);
            textRight = hintRight - pen.measure(hint, hintSize, false) - 24;
        } else {
            const std::string first = hint.substr(0, cut + cutLen);
            std::string second = hint.substr(cut + cutLen);
            while (!second.empty() && second[0] == ' ') second.erase(0, 1);
            const double size = std::min(fitSize(pen, first, 16, 12, hintMaxW, false),
                                         fitSize(pen, second, 16, 12, hintMaxW, false));
            pen.text(hintRight, kHeaderY + 36, first, size, kTextMuted, false, true);
            pen.text(hintRight, kHeaderY + 62, second, size, kTextMuted, false, true);
            textRight = hintRight - std::max(pen.measure(first, size, false), pen.measure(second, size, false)) - 24;
        }
    }
    // 1 行で入ればそのまま（補足があれば 20px 以上、見出しだけなら前と同じ 14px 以上で、本当に収まるとき）。
    // 入らなければ、ボタンの左に収まるよう折り返す（補足は新しい行から、最大 3 行、20px から 13px まで）。
    // それでも入らなければ 3 行目を「…」で切る。ボタンの下には描かない
    const double maxTextW = textRight - textLeft;
    const std::string oneLine = detail.empty() ? headline : headline + detailSep + detail;
    const double oneSize = fitSize(pen, oneLine, 22, 14, maxTextW, bold);
    if (oneSize >= (detail.empty() ? 14 : 20) && pen.measure(oneLine, oneSize, bold) <= maxTextW) {
        pen.text(textLeft, centerBaseline(kHeaderY, kHeaderH, oneSize), oneLine, oneSize, color, bold);
    } else {
        constexpr int kMaxLines = 3;
        double size = 20;
        std::vector<std::string> lines;
        for (;; size -= 1) {
            lines = wrapText(pen, headline, size, maxTextW, bold);
            if (!detail.empty()) {
                for (std::string& l : wrapText(pen, detail, size, maxTextW, bold)) lines.push_back(std::move(l));
            }
            bool fits = static_cast<int>(lines.size()) <= kMaxLines && lines.size() * size * 1.3 <= kHeaderH - 8;
            for (const std::string& l : lines) fits = fits && pen.measure(l, size, bold) <= maxTextW;
            if (fits || size <= 13) break;
        }
        if (static_cast<int>(lines.size()) > kMaxLines) {
            lines.resize(kMaxLines);
            lines.back() = withEllipsis(pen, lines.back(), size, maxTextW, bold);
        }
        for (std::string& l : lines) {
            if (pen.measure(l, size, bold) > maxTextW) l = withEllipsis(pen, l, size, maxTextW, bold);
        }
        const double lineH = size * 1.3;
        const double top = kHeaderY + (kHeaderH - lines.size() * lineH) / 2;
        for (size_t i = 0; i < lines.size(); ++i) {
            pen.text(textLeft, centerBaseline(top + i * lineH, lineH, size), lines[i], size, color, bold);
        }
    }
    // ［更新する］（確認へ・確認の実行）は強調ボタン（アクセントの塗り ＋ 濃い文字。✓ は付けない）
    for (const SettingsAction a : buttons) {
        const bool primary = a == SettingsAction::UpdateInstall || a == SettingsAction::UpdateConfirmYes;
        drawButton(pen, text, a, primary, 22, 0, false);
    }
}

double SettingsPanel::drawPageSwitch(const Pen& pen, const UiText& text, double left) {
    // Placement | Layout | Profiles
    const double y = kHeaderY + (kHeaderH - kButtonH) / 2;
    const double part = kPageSwitchW / 3;
    placeFooterButton(SettingsAction::PagePlacement, left, y, part, kButtonH);
    placeFooterButton(SettingsAction::PageLayout, left + part, y, part, kButtonH);
    placeFooterButton(SettingsAction::PageProfiles, left + 2 * part, y, part, kButtonH);
    const int selected = page_ == SettingsPage::Placement ? 0 : (page_ == SettingsPage::Layout ? 1 : 2);
    drawSegmented(pen, text, {SettingsAction::PagePlacement, SettingsAction::PageLayout, SettingsAction::PageProfiles},
                  selected, true);
    return left + kPageSwitchW;
}

void SettingsPanel::drawProfileButton(const Pen& pen, const UiText& text, SettingsAction action, double x, double y,
                                      double w, bool enabled, bool primary) {
    placeFooterButton(action, x, y, w, kButtonH);
    if (enabled && profileArmed_ != action) {
        drawButton(pen, text, action, primary, 22, 0, false);
        return;
    }
    if (!enabled) profileDisabled_.push_back(action);
    // disabled: faded, no border; armed (waiting for the second tap): red like Quit's confirm
    const bool armed = enabled;
    pen.color(armed ? kDanger : kControl);
    pen.roundedRect(x, y, w, kButtonH, 18);
    cairo_fill(pen.cr);
    const std::string label = labelOf(action, text);
    const double size = fitSize(pen, label, 22, 14, w - 20, true);
    textCentered(pen, x + w / 2, centerBaseline(y, kButtonH, size), label, size, armed ? kOnAccent : kTextDisabled, true);
}

void SettingsPanel::drawProfilesPage(const Pen& pen, const UiText& text, const ProfilesUiState& profiles) {
    lastProfiles_ = profiles;
    profileDisabled_.clear();
    profileRowIds_.clear();
    for (const auto& item : profiles.items) profileRowIds_.push_back(item.id);
    const auto has = [&](const std::string& id) {
        return std::find(profileRowIds_.begin(), profileRowIds_.end(), id) != profileRowIds_.end();
    };
    if (!has(selectedProfile_)) {
        selectedProfile_ = has(profiles.activeId) ? profiles.activeId
                           : profileRowIds_.empty() ? std::string()
                                                    : profileRowIds_.front();
    }
    // Rows that no longer exist mustn't stay hittable
    buttons_.erase(std::remove_if(buttons_.begin(), buttons_.end(),
                                  [](const Button& b) {
                                      return b.action >= SettingsAction::ProfileRow0 &&
                                             b.action <= SettingsAction::ProfileRow7;
                                  }),
                   buttons_.end());

    // The list
    const double lx = kPad + kCardPad;
    const double lw = kProfListW - 2 * kCardPad;
    drawCard(pen, kPad, kCardY, kProfListW, kCardH, 20, kCard, kCard, 0);
    pen.text(lx, kCardY + 42, text.cardProfiles, 24, kText, true);
    char count[32];
    std::snprintf(count, sizeof(count), text.profileCountFormat, profiles.items.size(), profiles.maxProfiles);
    pen.text(kPad + kProfListW - kCardPad - pen.measure(count, 18, false), kCardY + 42, count, 18, kTextMuted);
    if (profiles.items.empty()) pen.text(lx, kProfRowY0 + 30, text.profileNone, fitSize(pen, text.profileNone, 18, 12, lw, false), kTextMuted);
    for (size_t i = 0; i < profiles.items.size() && i < 8; ++i) {
        const auto& item = profiles.items[i];
        const SettingsAction action = static_cast<SettingsAction>(static_cast<int>(SettingsAction::ProfileRow0) + static_cast<int>(i));
        const double y = kProfRowY0 + static_cast<double>(i) * (kProfRowH + kProfRowGap);
        placeFooterButton(action, lx, y, lw, kProfRowH);
        const bool selected = item.id == selectedProfile_;
        const bool active = item.id == profiles.activeId;
        pen.color(selected || hover_ == action || pressed_ == action ? kControlHover : kControl);
        pen.roundedRect(lx, y, lw, kProfRowH, 12);
        cairo_fill(pen.cr);
        if (selected) strokeRounded(pen, lx, y, lw, kProfRowH, 12, kAccent, 2);
        // right: "Active" (+ "unsaved changes"), left: the name in what's left
        double right = lx + lw - 14;
        if (active) {
            if (profiles.unsaved) {
                right -= pen.measure(text.profileUnsaved, 16, true);
                pen.text(right, centerBaseline(y, kProfRowH, 16), text.profileUnsaved, 16, kWarn, true);
                right -= 14;
            }
            right -= pen.measure(text.profileActive, 16, true);
            pen.text(right, centerBaseline(y, kProfRowH, 16), text.profileActive, 16, kAccentText, true);
            right -= 16;
        }
        const double nameW = right - (lx + 16);
        pen.text(lx + 16, centerBaseline(y, kProfRowH, 20), item.name, fitSize(pen, item.name, 20, 13, nameW, active),
                 kText, active);
    }

    // The selected profile: its actions, then the live layout's state
    const double x = kProfCard2X + kCardPad;
    const double w = kProfCard2W - 2 * kCardPad;
    drawCard(pen, kProfCard2X, kCardY, kProfCard2W, kCardH, 20, kCard, kCard, 0);
    std::string title = "-";
    for (const auto& item : profiles.items) {
        if (item.id == selectedProfile_) title = item.name;
    }
    pen.text(x, kCardY + 42, title, fitSize(pen, title, 24, 14, w, true), kText, true);
    const bool haveSelected = !selectedProfile_.empty();
    const bool haveActive = has(profiles.activeId);
    const double bw = (w - 2 * kGap) / 3;
    const double row0 = kCardY + 70;
    const double row1 = row0 + kButtonH + kGap;
    drawProfileButton(pen, text, SettingsAction::ProfileUse, x, row0, bw, haveSelected, true);
    drawProfileButton(pen, text, SettingsAction::ProfileRename, x + bw + kGap, row0, bw, haveSelected, false);
    drawProfileButton(pen, text, SettingsAction::ProfileDelete, x + 2 * (bw + kGap), row0, bw, haveSelected, false);
    drawProfileButton(pen, text, SettingsAction::ProfileNew, x, row1, bw, profiles.items.size() < profiles.maxProfiles,
                      false);
    drawProfileButton(pen, text, SettingsAction::ProfileSave, x + bw + kGap, row1, bw, haveActive, false);
    drawProfileButton(pen, text, SettingsAction::ProfileReset, x + 2 * (bw + kGap), row1, bw, true, false);

    double y = row1 + kButtonH + 40;
    std::string state;
    Color stateColor = kTextMuted;
    if (haveActive) {
        std::string activeName;
        for (const auto& item : profiles.items) {
            if (item.id == profiles.activeId) activeName = item.name;
        }
        state = std::string(text.profileActive) + ": " + activeName +
                (profiles.unsaved ? std::string(" · ") + text.profileUnsaved : std::string());
        if (profiles.unsaved) stateColor = kWarn;
    } else if (profiles.unsaved) {
        state = text.profileNotSaved;
        stateColor = kWarn;
    }
    if (!state.empty()) {
        pen.text(x, y, state, fitSize(pen, state, 19, 13, w, true), stateColor, true);
        y += 30;
    }
    if (profileArmed_ == SettingsAction::ProfileUse || profileArmed_ == SettingsAction::ProfileReset) {
        pen.text(x, y, text.profileTapAgain, fitSize(pen, text.profileTapAgain, 18, 12, w, true), kDanger, true);
        y += 28;
    }
    if (!profiles.status.empty()) pen.text(x, y, profiles.status, fitSize(pen, profiles.status, 18, 12, w, false), kText);
    pen.text(x, kCardY + kCardH - kCardPad, text.profileHint, fitSize(pen, text.profileHint, 16, 11, w, false), kTextMuted);
}

void SettingsPanel::render(const Config& config, const AutostartStatus& autostart,
                            const frame_updater::UpdateStatus& update, const ProfilesUiState& profiles) {
    const Pen pen {cr_, &fonts_};
    const UiText& t = uiText(config.language);
    // 固定先・ページが変わったら、カードのボタンを置き直す（下の段と更新の帯はこのあと置き直す）
    const ButtonMode mode = page_ == SettingsPage::Layout                ? ButtonMode::Layout
                            : page_ == SettingsPage::Profiles           ? ButtonMode::Profiles
                            : config.attachment == Attachment::Head     ? ButtonMode::Head
                            : config.attachment == Attachment::Detached ? ButtonMode::Detached
                                                                        : ButtonMode::Wrist;
    if (mode != mode_) {
        mode_ = mode;
        layoutButtons();
    }
    editor_.sync(config.layout);
    editor_.setLanguage(widgetLanguage(config.language));

    // 地（不透明。コントラスト比は不透明な地で計算している）
    cairo_save(cr_);
    cairo_set_operator(cr_, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr_);
    cairo_restore(cr_);
    pen.color(kBg);
    pen.roundedRect(0, 0, kWidth, kHeight, 24);
    cairo_fill(cr_);

    // 上の行 = 見出し・新しい版の確認の帯・状態のピル、その下に左から パネル・位置・向き のカード、
    // 下 = 言語・自動起動・終了と説明
    const double pillX = drawHeader(pen, t, config);
    const double titleRight = kPad + pen.measure(t.settingsTitle, 34, true);
    // The page switch after the title, then the update bar
    const double barLeft = drawPageSwitch(pen, t, titleRight + 24) + 20;
    drawUpdateBar(pen, t, config, update, barLeft, pillX - 20);
    if (page_ == SettingsPage::Placement) {
        drawPanelCard(pen, t, config);
        drawPositionCard(pen, t, config);
        drawFacingCard(pen, t, config);
    }
    if (page_ == SettingsPage::Profiles) {
        drawProfilesPage(pen, t, profiles);
    } else {
        profileDisabled_.clear();  // its buttons are gone with the page (layoutButtons), so nothing to skip
    }
    drawFooter(pen, t, config, autostart);
    if (page_ == SettingsPage::Layout) editor_.render(pen, t);  // last: a dragged card is drawn over everything

    cairo_surface_flush(surface_);
}

const std::vector<uint8_t>& SettingsPanel::toRgba() {
    surfaceToRgba(surface_, rgba_);
    return rgba_;
}

bool SettingsPanel::writePng(const std::string& path) const {
    return cairo_surface_write_to_png(surface_, path.c_str()) == CAIRO_STATUS_SUCCESS;
}

int SettingsPanel::width() const {
    return kWidth;
}

int SettingsPanel::height() const {
    return kHeight;
}

void renderThumbnail(const FontSet& fonts, int size, std::vector<uint8_t>& rgba, const std::string& pngPath) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t* cr = cairo_create(surface);
    const Pen pen {cr, &fonts};
    const double s = size / 256.0;

    // 地と内側の縁
    pen.color(kBg);
    pen.roundedRect(8 * s, 8 * s, 240 * s, 240 * s, 48 * s);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
    cairo_set_line_width(cr, 2 * s);
    pen.roundedRect(9 * s, 9 * s, 238 * s, 238 * s, 47 * s);
    cairo_stroke(cr);

    // グラフの台（カードの色）と、目標の点線
    pen.color(kCard);
    pen.roundedRect(34 * s, 40 * s, 188 * s, 118 * s, 18 * s);
    cairo_fill(cr);
    pen.color(kTextMuted);
    cairo_set_line_width(cr, 3 * s);
    const double dashes[] = {8 * s, 7 * s};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_move_to(cr, 48 * s, 70 * s);
    cairo_line_to(cr, 208 * s, 70 * s);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // 折れ線（イメージカラー）と、その光彩
    const double ys[] = {128, 104, 116, 82, 94, 64, 76};
    /**
     * 折れ線の道筋を作る。
     */
    const auto path = [&]() {
        for (int i = 0; i < 7; ++i) {
            const double x = (48 + i * 26.7) * s;
            if (i == 0) cairo_move_to(cr, x, ys[i] * s); else cairo_line_to(cr, x, ys[i] * s);
        }
    };
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int i = 3; i >= 1; --i) {
        pen.color(kAccent, 0.10);
        cairo_set_line_width(cr, (10 + i * 7) * s);
        path();
        cairo_stroke(cr);
    }
    pen.color(kAccent);
    cairo_set_line_width(cr, 10 * s);
    path();
    cairo_stroke(cr);
    drawDisc(cr, (48 + 6 * 26.7) * s, 76 * s, 11 * s, kAccent);

    const double w = pen.measure("Perf", 56 * s, true);
    pen.text((size - w) / 2, 222 * s, "Perf", 56 * s, kText, true);

    cairo_surface_flush(surface);
    surfaceToRgba(surface, rgba);
    if (!pngPath.empty()) cairo_surface_write_to_png(surface, pngPath.c_str());
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
