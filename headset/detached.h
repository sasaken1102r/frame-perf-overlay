// Detached ("Room") maths. The panel sits at a fixed standing-space pose (Config::detachedPose); the laser grabs it
// while the dashboard is open. Pure maths on OpenVR's matrix types, no OpenVR calls, so it's testable off the Frame.
#pragma once

#include "config.h"
#include "openvr.h"

#include <cstdint>
#include <functional>
#include <vector>

/** Distance "Bring it in front of me" puts the panel at (m). */
constexpr double kDetachedFrontDistanceM = 1.0;
/** Step of the dashboard's move buttons in Detached mode (m), along the panel's own axes. */
constexpr double kDetachedNudgeM = 0.02;
/** Step of the Near / Far buttons in Detached mode (m). */
constexpr double kDetachedDepthStepM = 0.05;

/** the Detached curve (Config::detachedCurveDeg) is the arc the whole panel wraps, 0° (flat) to this. */
constexpr int kDetachedCurveMaxDeg = 90;
/** the dashboard's − / + step and the config's rounding (degrees). */
constexpr int kDetachedCurveStepDeg = 10;

/**
 * clamps a curve to 0..kDetachedCurveMaxDeg and rounds it to the nearest kDetachedCurveStepDeg.
 * @param degrees any value (NaN = 0)
 * @return 0, 10, ..., 90
 */
inline int clampCurveDeg(double degrees) {
    if (!(degrees > 0.0)) return 0;
    if (degrees >= kDetachedCurveMaxDeg) return kDetachedCurveMaxDeg;
    return static_cast<int>(degrees / kDetachedCurveStepDeg + 0.5) * kDetachedCurveStepDeg;
}

/**
 * SetOverlayCurvature's value for a panel wrapping `degrees` of arc: the fraction of a full cylinder, so 90° = 0.25.
 * The radius then follows the panel's width (R = width / angle): a wider layout wraps a bigger circle.
 * @param degrees the arc (clamped / rounded with clampCurveDeg)
 * @return 0 (flat) .. 0.25
 */
inline float detachedCurvature(int degrees) {
    return static_cast<float>(clampCurveDeg(degrees) / 360.0);
}

/** @return the pose as an OpenVR matrix */
vr::HmdMatrix34_t toHmdMatrix(const DetachedPose& pose);

/** @return the matrix as a valid pose (rotation re-orthonormalised, so rounding never builds up) */
DetachedPose fromHmdMatrix(const vr::HmdMatrix34_t& m);

/**
 * Inverse of a rigid transform (rotation + translation): [Rᵀ | −Rᵀt].
 * @param m the transform
 * @return its inverse
 */
vr::HmdMatrix34_t invertRigid(const vr::HmdMatrix34_t& m);

/**
 * Makes the rotation part orthonormal again (Gram-Schmidt on the z then y column, x = y × z). A degenerate
 * rotation becomes the identity.
 * @param m the transform (changed in place)
 */
void orthonormalize(vr::HmdMatrix34_t& m);

/**
 * "Bring it in front of me": kDetachedFrontDistanceM in front of the HMD along its gaze projected onto the floor
 * plane, at eye height, upright and facing the HMD. Looking straight up or down falls back to the HMD's own up axis.
 * @param hmd the HMD's standing-space pose
 * @return the pose
 */
DetachedPose placeInFront(const vr::HmdMatrix34_t& hmd);

/**
 * Turns the panel in place so its front (+z) faces the given point, upright (its x axis stays horizontal).
 * @param pose the pose (position kept)
 * @param x the point (standing space)
 * @param y
 * @param z
 * @return the turned pose (unchanged if the point is on top of it)
 */
DetachedPose faceTowards(const DetachedPose& pose, double x, double y, double z);

/**
 * Moves the panel along its own axis.
 * @param pose the pose
 * @param axis 0 = right, 1 = up, 2 = front (towards the viewer)
 * @param meters signed distance
 * @return the moved pose
 */
DetachedPose nudgeDetached(const DetachedPose& pose, int axis, double meters);

/**
 * Turns the panel about its own axis through its centre, with the head panel's sign conventions (panelRotation):
 * yaw + = the face turns right, pitch + = the face turns up, roll + = counter-clockwise seen from the front.
 * @param pose the pose
 * @param axis 0 = pitch, 1 = yaw, 2 = roll
 * @param degrees signed angle
 * @return the turned pose
 */
DetachedPose rotateDetached(const DetachedPose& pose, int axis, double degrees);

/** The laser pressing / releasing on the panel itself (only while it takes mouse input: Detached + dashboard open). */
struct GrabInput {
    bool down = false;    ///< true = press, false = release
    uint32_t device = 0;  ///< the pointing device (vr::TrackedDeviceIndex_t)
};

/**
 * One laser grab of the Detached panel: on press it remembers the panel's pose relative to the pointing device, and while
 * held the panel follows the device rigidly (device pose × grab offset).
 */
class DetachedGrab {
public:
    /**
     * Starts a grab.
     * @param device the pointing device (tracked device index)
     * @param devicePose its standing-space pose
     * @param panel the panel's pose now
     * @return false (and no grab) if the panel pose isn't valid
     */
    bool begin(uint32_t device, const vr::HmdMatrix34_t& devicePose, const DetachedPose& panel);

    /**
     * @param devicePose the device's standing-space pose now
     * @return where the panel goes: devicePose × offset
     */
    DetachedPose follow(const vr::HmdMatrix34_t& devicePose) const;

    /** Ends the grab (the caller saves the last pose it applied). */
    void end() { active_ = false; }

    /** @return true while held */
    bool active() const { return active_; }

    /** @return the device holding it (only meaningful while active) */
    uint32_t device() const { return device_; }

private:
    bool active_ = false;
    uint32_t device_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::HmdMatrix34_t offset_ {};  ///< device → panel
};

/** What one stepGrab pass did. */
struct GrabStep {
    bool released = false;    ///< a grab ended: save pose (it's where the panel was last shown)
    bool laserMoved = false;  ///< ...because the dashboard's laser went to another controller
    bool moved = false;       ///< pose follows the laser: show it there now
};

/**
 * One main-loop pass of laser input on the Detached panel, in this order: endGrab ends the grab first (so nothing
 * after it is followed, and pose stays as it is), then presses and releases as they came, then the laser check, then
 * the follow.
 *
 * The laser check ends the grab when the dashboard's laser is on another controller than the grabbing one (its grip
 * moves the laser, even for a moment, and then SteamVR never sends the mouse-up). An invalid laserDevice is ignored.
 * @param grab the grab (changed)
 * @param inputs this pass's presses / releases on the panel
 * @param endGrab end any grab now: the dashboard closed (VREvent_DashboardDeactivated), or a dashboard setting
 *                changed this pass (its pose / attach point is already in pose / the config)
 * @param interactive the panel takes the laser now (a press starts a grab only then)
 * @param devicePose a device's standing-space pose now (false = not tracked)
 * @param pose the room pose (changed while a grab follows; kept as it is on release)
 * @param laserDevice the controller with the dashboard's laser now (IVROverlay::GetPrimaryDashboardDevice;
 *                    vr::k_unTrackedDeviceIndexInvalid = unknown, no check)
 * @return what happened
 */
GrabStep stepGrab(DetachedGrab& grab, const std::vector<GrabInput>& inputs, bool endGrab, bool interactive,
                  const std::function<bool(uint32_t, vr::HmdMatrix34_t&)>& devicePose, DetachedPose& pose,
                  uint32_t laserDevice = vr::k_unTrackedDeviceIndexInvalid);
