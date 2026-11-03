// Detached ("Room") maths.
#include "detached.h"

#include "placement.h"  // composeTransform

#include <cmath>

namespace {

constexpr double kDegToRad = M_PI / 180.0;

struct Vec3 {
    double x, y, z;
};

Vec3 column(const vr::HmdMatrix34_t& m, int c) {
    return {m.m[0][c], m.m[1][c], m.m[2][c]};
}

void setColumn(vr::HmdMatrix34_t& m, int c, const Vec3& v) {
    m.m[0][c] = static_cast<float>(v.x);
    m.m[1][c] = static_cast<float>(v.y);
    m.m[2][c] = static_cast<float>(v.z);
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

/** @return v / |v|, or false if v is (almost) zero */
bool normalize(Vec3& v) {
    const double len = length(v);
    if (len < 1e-6) return false;
    v = {v.x / len, v.y / len, v.z / len};
    return true;
}

vr::HmdMatrix34_t identity() {
    vr::HmdMatrix34_t m {};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
    return m;
}

/** Builds a pose from three orthonormal axes and a position. */
DetachedPose poseFrom(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& p) {
    DetachedPose out;
    out.valid = true;
    const Vec3 axes[3] = {x, y, z};
    for (int c = 0; c < 3; ++c) {
        out.m[0][c] = axes[c].x;
        out.m[1][c] = axes[c].y;
        out.m[2][c] = axes[c].z;
    }
    out.m[0][3] = p.x;
    out.m[1][3] = p.y;
    out.m[2][3] = p.z;
    return out;
}

}  // namespace

vr::HmdMatrix34_t toHmdMatrix(const DetachedPose& pose) {
    vr::HmdMatrix34_t m {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) m.m[r][c] = static_cast<float>(pose.m[r][c]);
    }
    return m;
}

DetachedPose fromHmdMatrix(const vr::HmdMatrix34_t& in) {
    vr::HmdMatrix34_t m = in;
    orthonormalize(m);
    DetachedPose pose;
    pose.valid = true;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) pose.m[r][c] = m.m[r][c];
    }
    return pose;
}

vr::HmdMatrix34_t invertRigid(const vr::HmdMatrix34_t& m) {
    vr::HmdMatrix34_t out {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out.m[r][c] = m.m[c][r];
    }
    for (int r = 0; r < 3; ++r) {
        out.m[r][3] = -(out.m[r][0] * m.m[0][3] + out.m[r][1] * m.m[1][3] + out.m[r][2] * m.m[2][3]);
    }
    return out;
}

void orthonormalize(vr::HmdMatrix34_t& m) {
    Vec3 z = column(m, 2);
    Vec3 y = column(m, 1);
    if (!normalize(z)) {
        const vr::HmdMatrix34_t id = identity();
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m.m[r][c] = id.m[r][c];
        }
        return;
    }
    const double d = dot(y, z);
    y = {y.x - d * z.x, y.y - d * z.y, y.z - d * z.z};
    if (!normalize(y)) {
        // y was parallel to z: pick any perpendicular
        y = std::fabs(z.y) < 0.9 ? cross(z, cross(Vec3 {0, 1, 0}, z)) : cross(z, cross(Vec3 {1, 0, 0}, z));
        normalize(y);
    }
    const Vec3 x = cross(y, z);
    setColumn(m, 0, x);
    setColumn(m, 1, y);
    setColumn(m, 2, z);
}

DetachedPose faceTowards(const DetachedPose& pose, double tx, double ty, double tz) {
    const Vec3 p {pose.m[0][3], pose.m[1][3], pose.m[2][3]};
    Vec3 z {tx - p.x, ty - p.y, tz - p.z};
    if (!normalize(z)) return pose;
    // Upright: the x axis stays horizontal (up × z). Straight above / below: keep the current x axis
    Vec3 x = cross(Vec3 {0, 1, 0}, z);
    if (!normalize(x)) {
        x = {pose.m[0][0], pose.m[1][0], pose.m[2][0]};
        const double d = dot(x, z);
        x = {x.x - d * z.x, x.y - d * z.y, x.z - d * z.z};
        if (!normalize(x)) x = {1, 0, 0};
    }
    const Vec3 y = cross(z, x);
    return poseFrom(x, y, z, p);
}

DetachedPose placeInFront(const vr::HmdMatrix34_t& hmd) {
    const Vec3 eye {hmd.m[0][3], hmd.m[1][3], hmd.m[2][3]};
    // Gaze = the HMD's −z. Flatten it. Looking straight down, the top of the head (+y) points forward; looking
    // straight up, it points backward
    const Vec3 back = column(hmd, 2);
    Vec3 forward = {-back.x, 0.0, -back.z};
    if (!normalize(forward)) {
        const Vec3 up = column(hmd, 1);
        const double sign = -back.y < 0 ? 1.0 : -1.0;
        forward = {sign * up.x, 0.0, sign * up.z};
        if (!normalize(forward)) forward = {0, 0, -1};
    }
    DetachedPose at;
    at.valid = true;
    at.m[0][3] = eye.x + forward.x * kDetachedFrontDistanceM;
    at.m[1][3] = eye.y;
    at.m[2][3] = eye.z + forward.z * kDetachedFrontDistanceM;
    return faceTowards(at, eye.x, eye.y, eye.z);
}

DetachedPose nudgeDetached(const DetachedPose& pose, int axis, double meters) {
    DetachedPose out = pose;
    for (int r = 0; r < 3; ++r) out.m[r][3] += pose.m[r][axis] * meters;
    return out;
}

DetachedPose rotateDetached(const DetachedPose& pose, int axis, double degrees) {
    // Same conventions as panelRotation: pitch is Rx(−pitch), yaw Ry(yaw), roll Rz(roll), applied in the panel's frame
    const double a = (axis == 0 ? -degrees : degrees) * kDegToRad;
    const double c = std::cos(a), s = std::sin(a);
    vr::HmdMatrix34_t local = identity();
    const int i = axis == 0 ? 1 : (axis == 1 ? 2 : 0);  // the plane that turns: x→(y,z), y→(z,x), z→(x,y)
    const int j = axis == 0 ? 2 : (axis == 1 ? 0 : 1);
    local.m[i][i] = static_cast<float>(c);
    local.m[i][j] = static_cast<float>(-s);
    local.m[j][i] = static_cast<float>(s);
    local.m[j][j] = static_cast<float>(c);
    return fromHmdMatrix(composeTransform(toHmdMatrix(pose), local));
}

bool DetachedGrab::begin(uint32_t device, const vr::HmdMatrix34_t& devicePose, const DetachedPose& panel) {
    active_ = false;
    if (!panel.valid) return false;
    offset_ = composeTransform(invertRigid(devicePose), toHmdMatrix(panel));
    device_ = device;
    active_ = true;
    return true;
}

DetachedPose DetachedGrab::follow(const vr::HmdMatrix34_t& devicePose) const {
    return fromHmdMatrix(composeTransform(devicePose, offset_));
}

GrabStep stepGrab(DetachedGrab& grab, const std::vector<GrabInput>& inputs, bool endGrab, bool interactive,
                  const std::function<bool(uint32_t, vr::HmdMatrix34_t&)>& devicePose, DetachedPose& pose,
                  uint32_t laserDevice) {
    GrabStep step;
    if (endGrab) {
        // A dashboard close counts as a release even with the laser still pressed (no mouse-up may come); a setting
        // changed meanwhile wins over the drag
        interactive = false;
        if (grab.active()) {
            grab.end();
            step.released = true;
        }
    }
    for (const GrabInput& g : inputs) {
        if (g.down && !grab.active() && interactive) {
            vr::HmdMatrix34_t device {};
            if (devicePose(g.device, device)) grab.begin(g.device, device, pose);
        } else if (!g.down && grab.active()) {
            grab.end();
            step.released = true;
        }
    }
    if (grab.active() && laserDevice != vr::k_unTrackedDeviceIndexInvalid && laserDevice != grab.device()) {
        grab.end();
        step.released = true;
        step.laserMoved = true;
    }
    if (grab.active()) {
        vr::HmdMatrix34_t device {};

        if (devicePose(grab.device(), device)) {
            pose = grab.follow(device);
            step.moved = true;
        }
    }
    return step;
}
