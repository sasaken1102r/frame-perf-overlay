// Room (Attachment::Detached): the pose maths (grab, place in front, face, nudge, rotate, curve) and the config's
// detached_pose / detached_curve_deg keys.
// Pure maths on OpenVR's matrix type; no SteamVR needed.
#include "config.h"
#include "detached.h"
#include "placement.h"

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FAIL line %d: %s\n", __LINE__, #expr);                  \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

constexpr double kEps = 2e-4;  // matrices are floats

struct Vec3 {
    double x, y, z;
};

Vec3 col(const vr::HmdMatrix34_t& m, int c) {
    return {m.m[0][c], m.m[1][c], m.m[2][c]};
}
Vec3 col(const DetachedPose& p, int c) {
    return {p.m[0][c], p.m[1][c], p.m[2][c]};
}
double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(const Vec3& v) {
    return std::sqrt(dot(v, v));
}
bool near(double a, double b, double eps = kEps) {
    return std::fabs(a - b) < eps;
}
bool nearVec(const Vec3& a, const Vec3& b, double eps = kEps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool nearMatrix(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b, double eps = kEps) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!near(a.m[r][c], b.m[r][c], eps)) return false;
        }
    }
    return true;
}
bool nearPose(const DetachedPose& a, const DetachedPose& b, double eps = kEps) {
    return nearMatrix(toHmdMatrix(a), toHmdMatrix(b), eps);
}

/** @return true if the rotation part is orthonormal and right-handed */
bool isRotation(const vr::HmdMatrix34_t& m, double eps = kEps) {
    const Vec3 x = col(m, 0), y = col(m, 1), z = col(m, 2);
    return near(len(x), 1, eps) && near(len(y), 1, eps) && near(len(z), 1, eps) && near(dot(x, y), 0, eps) &&
           near(dot(y, z), 0, eps) && near(dot(x, z), 0, eps) && nearVec(cross(x, y), z, eps);
}

/** Rotation about the y axis (a yaw of the whole body), then a position. */
vr::HmdMatrix34_t yawPose(double yawDeg, double x, double y, double z) {
    const double a = yawDeg * M_PI / 180.0;
    vr::HmdMatrix34_t m {};
    m.m[0][0] = static_cast<float>(std::cos(a));
    m.m[0][2] = static_cast<float>(std::sin(a));
    m.m[1][1] = 1.0f;
    m.m[2][0] = static_cast<float>(-std::sin(a));
    m.m[2][2] = static_cast<float>(std::cos(a));
    m.m[0][3] = static_cast<float>(x);
    m.m[1][3] = static_cast<float>(y);
    m.m[2][3] = static_cast<float>(z);
    return m;
}

/** An arbitrary rigid pose (rotation from panelRotation, so it's a true rotation). */
vr::HmdMatrix34_t anyPose(double yaw, double pitch, double roll, double x, double y, double z) {
    double r[3][3];
    panelRotation(yaw, pitch, roll, r);
    vr::HmdMatrix34_t m {};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) m.m[i][j] = static_cast<float>(r[i][j]);
    }
    m.m[0][3] = static_cast<float>(x);
    m.m[1][3] = static_cast<float>(y);
    m.m[2][3] = static_cast<float>(z);
    return m;
}

vr::HmdMatrix34_t identityMatrix() {
    return yawPose(0, 0, 0, 0);
}

// ---- Pose maths ----

void testConvert() {
    const vr::HmdMatrix34_t m = anyPose(25, -10, 40, 0.3, 1.1, -0.7);
    const DetachedPose p = fromHmdMatrix(m);
    CHECK(p.valid);
    CHECK(nearMatrix(toHmdMatrix(p), m));
    for (int r = 0; r < 3; ++r) CHECK(near(p.m[r][3], m.m[r][3], 1e-6));  // position untouched
    // A slightly skewed rotation comes out orthonormal
    vr::HmdMatrix34_t skew = m;
    skew.m[0][0] *= 1.01f;
    skew.m[1][2] += 0.02f;
    CHECK(!isRotation(skew, 1e-3));
    CHECK(isRotation(toHmdMatrix(fromHmdMatrix(skew))));
}

void testInvertRigid() {
    const vr::HmdMatrix34_t poses[] = {identityMatrix(), yawPose(90, 1, 2, 3), anyPose(30, 20, -50, -0.4, 1.7, 2.2),
                                       anyPose(-170, -80, 10, 5, -3, 0.5)};
    for (const vr::HmdMatrix34_t& m : poses) {
        const vr::HmdMatrix34_t inv = invertRigid(m);
        CHECK(nearMatrix(composeTransform(m, inv), identityMatrix()));
        CHECK(nearMatrix(composeTransform(inv, m), identityMatrix()));
        CHECK(nearMatrix(invertRigid(inv), m));
    }
    // A pure translation inverts to the opposite one
    vr::HmdMatrix34_t t = identityMatrix();
    t.m[0][3] = 1.5f;
    t.m[2][3] = -2.0f;
    const vr::HmdMatrix34_t inv = invertRigid(t);
    CHECK(near(inv.m[0][3], -1.5) && near(inv.m[1][3], 0) && near(inv.m[2][3], 2.0));
}

void testOrthonormalize() {
    // Already a rotation: unchanged
    vr::HmdMatrix34_t m = anyPose(40, 15, -25, 1, 2, 3);
    vr::HmdMatrix34_t copy = m;
    orthonormalize(m);
    CHECK(nearMatrix(m, copy, 1e-5));

    // Scaled and sheared: orthonormal again, the z axis (the face) kept, translation untouched
    vr::HmdMatrix34_t bad = copy;
    for (int r = 0; r < 3; ++r) {
        bad.m[r][0] *= 1.2f;
        bad.m[r][1] = bad.m[r][1] * 0.9f + bad.m[r][0] * 0.1f;
    }
    const Vec3 z = col(bad, 2);
    orthonormalize(bad);
    CHECK(isRotation(bad));
    CHECK(nearVec(col(bad, 2), {z.x / len(z), z.y / len(z), z.z / len(z)}));
    for (int r = 0; r < 3; ++r) CHECK(near(bad.m[r][3], copy.m[r][3], 1e-6));

    // z column zero: identity rotation
    vr::HmdMatrix34_t zero {};
    zero.m[0][0] = 1.0f;
    zero.m[1][1] = 1.0f;
    zero.m[0][3] = 4.0f;
    orthonormalize(zero);
    CHECK(isRotation(zero));
    CHECK(nearMatrix(zero, [] {
        vr::HmdMatrix34_t id = identityMatrix();
        id.m[0][3] = 4.0f;
        return id;
    }()));

    // y parallel to z: still a valid rotation
    vr::HmdMatrix34_t par {};
    par.m[0][0] = 1.0f;
    par.m[1][1] = 1.0f;
    par.m[1][2] = 1.0f;  // z = (0,1,0), y = (0,1,0)
    orthonormalize(par);
    CHECK(isRotation(par));
    CHECK(nearVec(col(par, 2), {0, 1, 0}));

    // all zero
    vr::HmdMatrix34_t nothing {};
    orthonormalize(nothing);
    CHECK(isRotation(nothing));

    // Repeated compose + orthonormalize doesn't drift
    DetachedPose p = fromHmdMatrix(anyPose(10, 5, 3, 0, 1, -1));
    for (int i = 0; i < 2000; ++i) p = rotateDetached(p, i % 3, 0.7);
    CHECK(isRotation(toHmdMatrix(p)));
}

void testGrabKeepsThePanelRigid() {
    const vr::HmdMatrix34_t device = anyPose(20, -35, 10, 0.2, 1.0, -0.4);
    const DetachedPose panel = fromHmdMatrix(anyPose(-60, 5, 0, 0.4, 1.5, -1.2));
    DetachedGrab grab;
    CHECK(!grab.active());
    CHECK(grab.begin(7, device, panel));
    CHECK(grab.active() && grab.device() == 7);
    // begin + follow with the same device pose returns the original pose
    CHECK(nearPose(grab.follow(device), panel));
    CHECK(grab.follow(device).valid);

    // Move and turn the device: the panel follows rigidly (its pose in the device's frame stays the same)
    const vr::HmdMatrix34_t offset = composeTransform(invertRigid(device), toHmdMatrix(panel));
    const vr::HmdMatrix34_t devices[] = {yawPose(0, 1, 0, 0), anyPose(100, 30, -20, -1, 0.3, 2), anyPose(-45, -60, 80, 0, 2, 0),
                                         yawPose(180, 0, 0, 0)};
    for (const vr::HmdMatrix34_t& d : devices) {
        const DetachedPose followed = grab.follow(d);
        CHECK(followed.valid);
        CHECK(isRotation(toHmdMatrix(followed)));
        CHECK(nearMatrix(toHmdMatrix(followed), composeTransform(d, offset), 5e-4));
        CHECK(nearMatrix(composeTransform(invertRigid(d), toHmdMatrix(followed)), offset, 5e-4));
    }
    // A pure translation of the device moves the panel by the same amount, rotation unchanged
    vr::HmdMatrix34_t moved = device;
    moved.m[0][3] += 0.5f;
    moved.m[1][3] -= 0.25f;
    const DetachedPose shifted = grab.follow(moved);
    CHECK(near(shifted.m[0][3], panel.m[0][3] + 0.5) && near(shifted.m[1][3], panel.m[1][3] - 0.25) &&
          near(shifted.m[2][3], panel.m[2][3]));
    CHECK(nearVec(col(shifted, 0), col(panel, 0)) && nearVec(col(shifted, 2), col(panel, 2)));
    // A turn of the device about its own position swings the panel around it, radius kept
    const vr::HmdMatrix34_t turned = composeTransform(yawPose(90, device.m[0][3], device.m[1][3], device.m[2][3]),
                                                      [&] {
                                                          vr::HmdMatrix34_t r = device;
                                                          r.m[0][3] = r.m[1][3] = r.m[2][3] = 0;
                                                          return r;
                                                      }());
    const DetachedPose swung = grab.follow(turned);
    const Vec3 before {panel.m[0][3] - device.m[0][3], panel.m[1][3] - device.m[1][3], panel.m[2][3] - device.m[2][3]};
    const Vec3 after {swung.m[0][3] - device.m[0][3], swung.m[1][3] - device.m[1][3], swung.m[2][3] - device.m[2][3]};
    CHECK(near(len(before), len(after), 1e-3));

    // end() releases
    grab.end();
    CHECK(!grab.active());

    // A second grab replaces the first one's offset
    CHECK(grab.begin(3, moved, panel));
    CHECK(grab.device() == 3 && nearPose(grab.follow(moved), panel));

    // An invalid panel pose can't be grabbed (and clears a previous grab)
    DetachedPose invalid;
    CHECK(grab.active());
    CHECK(!grab.begin(3, device, invalid));
    CHECK(!grab.active());
}

// stepGrab: one main-loop pass of laser input. The dashboard closing ends the grab even with the mouse still down
void testStepGrab() {
    const vr::HmdMatrix34_t A = anyPose(20, -35, 10, 0.2, 1.0, -0.4);
    const vr::HmdMatrix34_t B = anyPose(50, -10, 0, 0.6, 1.2, -0.9);
    const vr::HmdMatrix34_t C = anyPose(-80, 15, 5, -1.0, 0.5, 0.7);
    const DetachedPose start = fromHmdMatrix(anyPose(-60, 5, 0, 0.4, 1.5, -1.2));
    const vr::HmdMatrix34_t offset = composeTransform(invertRigid(A), toHmdMatrix(start));

    std::map<uint32_t, vr::HmdMatrix34_t> devices;  // missing = not tracked
    const auto devicePose = [&](uint32_t d, vr::HmdMatrix34_t& out) {
        const auto it = devices.find(d);
        if (it == devices.end()) return false;
        out = it->second;
        return true;
    };
    const GrabInput press {true, 1}, release {false, 1};

    // The dashboard closes with no mouse-up
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        GrabStep s = stepGrab(grab, {press}, false, true, devicePose, pose);
        CHECK(grab.active() && s.moved && !s.released && nearPose(pose, start));

        devices[1] = B;
        s = stepGrab(grab, {}, false, true, devicePose, pose);
        const DetachedPose atB = fromHmdMatrix(composeTransform(B, offset));
        CHECK(grab.active() && s.moved && !s.released && nearPose(pose, atB, 5e-4));

        devices[1] = C;
        s = stepGrab(grab, {}, true, false, devicePose, pose);
        CHECK(s.released && !s.moved && !grab.active());
        CHECK(pose == atB);  // kept at the last followed pose, not dragged to C

        // Device keeps moving: nothing follows, nothing restarts
        devices[1] = A;
        s = stepGrab(grab, {}, false, true, devicePose, pose);
        CHECK(!s.released && !s.moved && !grab.active() && pose == atB);

        // A press in the same pass as the close doesn't start a grab
        s = stepGrab(grab, {press}, true, true, devicePose, pose);
        CHECK(!s.released && !s.moved && !grab.active() && pose == atB);
    }
    // A normal mouse-up keeps the last followed pose
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose);
        devices[1] = B;
        stepGrab(grab, {}, false, true, devicePose, pose);
        const DetachedPose atB = pose;
        devices[1] = C;
        const GrabStep s = stepGrab(grab, {release}, false, true, devicePose, pose);
        CHECK(s.released && !s.moved && !grab.active() && pose == atB);
    }
    // No grab: not interactive, device not tracked, panel never placed
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        GrabStep s = stepGrab(grab, {press}, false, false, devicePose, pose);
        CHECK(!grab.active() && !s.moved && !s.released && pose == start);

        devices.erase(1);
        s = stepGrab(grab, {press}, false, true, devicePose, pose);
        CHECK(!grab.active() && !s.moved && !s.released && pose == start);

        devices[1] = A;
        DetachedPose unplaced;
        s = stepGrab(grab, {press}, false, true, devicePose, unplaced);
        CHECK(!grab.active() && !s.moved && !s.released && !unplaced.valid);
    }
}

// stepGrab: the dashboard's laser going to another controller ends a grab with no mouse-up, and a setting changed
// meanwhile ends it too
void testStepGrabLaserAndActions() {
    const vr::HmdMatrix34_t A = anyPose(20, -35, 10, 0.2, 1.0, -0.4);
    const vr::HmdMatrix34_t B = anyPose(50, -10, 0, 0.6, 1.2, -0.9);
    const vr::HmdMatrix34_t C = anyPose(-80, 15, 5, -1.0, 0.5, 0.7);
    const DetachedPose start = fromHmdMatrix(anyPose(-60, 5, 0, 0.4, 1.5, -1.2));
    const DetachedPose action = fromHmdMatrix(anyPose(10, 0, 0, 0.0, 1.6, -1.0));
    const vr::HmdMatrix34_t offset = composeTransform(invertRigid(A), toHmdMatrix(start));
    const uint32_t none = vr::k_unTrackedDeviceIndexInvalid;

    std::map<uint32_t, vr::HmdMatrix34_t> devices;
    const auto devicePose = [&](uint32_t d, vr::HmdMatrix34_t& out) {
        const auto it = devices.find(d);
        if (it == devices.end()) return false;
        out = it->second;
        return true;
    };
    const GrabInput press {true, 1}, release {false, 1};

    // The laser goes to another controller mid-grab: the grab ends, nothing follows, pose stays at the last followed one
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        GrabStep s = stepGrab(grab, {press}, false, true, devicePose, pose, 1);
        CHECK(grab.active() && s.moved && !s.released && !s.laserMoved);
        devices[1] = B;
        s = stepGrab(grab, {}, false, true, devicePose, pose, 1);
        const DetachedPose atB = fromHmdMatrix(composeTransform(B, offset));
        CHECK(grab.active() && s.moved && !s.released && nearPose(pose, atB, 5e-4));
        const DetachedPose followed = pose;
        devices[1] = C;
        s = stepGrab(grab, {}, false, true, devicePose, pose, 2);
        CHECK(s.released && s.laserMoved && !s.moved && !grab.active() && pose == followed);
        // The laser stays away, or comes back, and the device keeps moving: nothing follows, nothing restarts
        for (uint32_t laser : {2u, 1u}) {
            devices[1] = A;
            s = stepGrab(grab, {}, false, true, devicePose, pose, laser);
            CHECK(!s.released && !s.laserMoved && !s.moved && !grab.active() && pose == followed);
        }
    }
    // An invalid laser device, or the grabbing one, doesn't end the grab; a mouse-up still does, without laserMoved
    for (uint32_t laser : {none, 1u}) {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose, laser);
        for (int i = 0; i < 4; i++) {
            devices[1] = i % 2 ? B : C;
            const GrabStep s = stepGrab(grab, {}, false, true, devicePose, pose, laser);
            CHECK(grab.active() && s.moved && !s.released && !s.laserMoved);
        }
        const GrabStep s = stepGrab(grab, {release}, false, true, devicePose, pose, laser);
        CHECK(s.released && !s.laserMoved && !grab.active());
    }
    // A blip: the laser is on device 2 for one pass and back on 1. The grab ends once, then doesn't restart
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose, 1);
        devices[1] = B;
        GrabStep s = stepGrab(grab, {}, false, true, devicePose, pose, 2);
        CHECK(s.released && s.laserMoved && !grab.active());
        const DetachedPose kept = pose;
        devices[1] = C;
        s = stepGrab(grab, {}, false, true, devicePose, pose, 1);
        CHECK(!s.released && !s.laserMoved && !s.moved && !grab.active() && pose == kept);
    }
    // Order: a mouse-up and the laser on another controller in one pass release once, and it isn't laserMoved
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose, 1);
        devices[1] = B;
        GrabStep s = stepGrab(grab, {release}, false, true, devicePose, pose, 2);
        CHECK(s.released && !s.laserMoved && !s.moved && !grab.active());
        s = stepGrab(grab, {}, false, true, devicePose, pose, 2);
        CHECK(!s.released && !s.laserMoved && !s.moved);
    }
    // A Placement action during a grab: main applies it to pose first, then endGrab ends the grab and keeps it
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose);
        devices[1] = B;
        pose = action;
        GrabStep s = stepGrab(grab, {press}, true, true, devicePose, pose);  // press in the same pass: no restart
        CHECK(s.released && !s.moved && !grab.active() && pose == action);
        s = stepGrab(grab, {}, false, true, devicePose, pose);
        CHECK(!s.released && !s.moved && !grab.active() && pose == action);
    }
    // An attach change during a grab: pose unchanged, the grab ends, nothing follows afterwards
    {
        DetachedGrab grab;
        DetachedPose pose = start;
        devices[1] = A;
        stepGrab(grab, {press}, false, true, devicePose, pose);
        const DetachedPose before = pose;
        devices[1] = B;
        GrabStep s = stepGrab(grab, {}, true, true, devicePose, pose);
        CHECK(s.released && !s.moved && !grab.active() && pose == before);
        devices[1] = C;
        s = stepGrab(grab, {}, false, true, devicePose, pose);
        CHECK(!s.moved && !s.released && pose == before);
    }
}

void testPlaceInFront() {
    // Standing, looking along -z (the default): the panel is 1 m ahead at eye height, upright, facing back
    {
        const vr::HmdMatrix34_t hmd = identityMatrix();
        vr::HmdMatrix34_t eye = hmd;
        eye.m[0][3] = 0.3f;
        eye.m[1][3] = 1.6f;
        eye.m[2][3] = -0.2f;
        const DetachedPose p = placeInFront(eye);
        CHECK(p.valid);
        CHECK(isRotation(toHmdMatrix(p)));
        CHECK(near(p.m[0][3], 0.3) && near(p.m[1][3], 1.6) && near(p.m[2][3], -1.2));
        CHECK(nearVec(col(p, 1), {0, 1, 0}));
        CHECK(nearVec(col(p, 2), {0, 0, 1}));  // front (+z) toward the viewer
    }
    // Turned by 40°, with a pitch up and a roll of the head: gaze is flattened, distance and height as asked
    {
        const vr::HmdMatrix34_t hmd = anyPose(40, 25, 12, -1.0, 1.7, 0.5);
        const DetachedPose p = placeInFront(hmd);
        CHECK(isRotation(toHmdMatrix(p)));
        const Vec3 eye {-1.0, 1.7, 0.5};
        const Vec3 d {p.m[0][3] - eye.x, p.m[1][3] - eye.y, p.m[2][3] - eye.z};
        CHECK(near(d.y, 0));
        CHECK(near(std::hypot(d.x, d.z), kDetachedFrontDistanceM));
        // In the direction of the gaze's floor projection (the HMD's −z, flattened)
        const Vec3 gaze = col(hmd, 2);
        const double gx = -gaze.x, gz = -gaze.z, gl = std::hypot(gx, gz);
        CHECK(near(d.x, gx / gl * kDetachedFrontDistanceM, 1e-3) && near(d.z, gz / gl * kDetachedFrontDistanceM, 1e-3));
        // Upright and facing the HMD
        CHECK(nearVec(col(p, 1), {0, 1, 0}, 1e-3));
        CHECK(near(col(p, 0).y, 0, 1e-3));
        const Vec3 toEye {-d.x, 0, -d.z};
        CHECK(nearVec(col(p, 2), {toEye.x / len(toEye), 0, toEye.z / len(toEye)}, 1e-3));
    }
    // Looking straight down: the top of the head points the way the user faces
    {
        vr::HmdMatrix34_t down {};
        down.m[0][0] = 1.0f;   // x = (1, 0, 0)
        down.m[2][1] = -1.0f;  // y = (0, 0, -1)
        down.m[1][2] = 1.0f;   // z = (0, 1, 0): the gaze (−z) is straight down
        down.m[1][3] = 1.5f;
        CHECK(isRotation(down));
        const DetachedPose p = placeInFront(down);
        CHECK(p.valid && isRotation(toHmdMatrix(p)));
        CHECK(near(p.m[0][3], 0) && near(p.m[1][3], 1.5) && near(p.m[2][3], -1.0));
        CHECK(nearVec(col(p, 1), {0, 1, 0}) && nearVec(col(p, 2), {0, 0, 1}));
    }
    // Looking straight up (head tipped back: the top of the head points BEHIND the user). The panel still goes
    // in front of them (along −z here).
    {
        vr::HmdMatrix34_t up {};
        up.m[0][0] = 1.0f;    // x = (1, 0, 0)
        up.m[2][1] = 1.0f;    // y = (0, 0, 1)
        up.m[1][2] = -1.0f;   // z = (0, -1, 0): the gaze (−z) is straight up
        up.m[1][3] = 1.5f;
        CHECK(isRotation(up));
        const DetachedPose p = placeInFront(up);
        CHECK(p.valid && isRotation(toHmdMatrix(p)));
        CHECK(near(p.m[1][3], 1.5));
        CHECK(near(std::hypot(p.m[0][3], p.m[2][3]), kDetachedFrontDistanceM));  // 1 m away, whichever side
        CHECK(near(p.m[0][3], 0) && near(p.m[2][3], -1.0));  // in front (-z), not behind
        CHECK(nearVec(col(p, 1), {0, 1, 0}) && nearVec(col(p, 2), {0, 0, 1}));
    }
}

void testFaceTowards() {
    DetachedPose start;
    start.valid = true;
    start.m[0][3] = 1;
    start.m[1][3] = 1.2;
    start.m[2][3] = -2;

    // Target to the side at the same height: z points at it, upright, x horizontal
    const DetachedPose a = faceTowards(start, 3, 1.2, 0);
    CHECK(a.valid && isRotation(toHmdMatrix(a)));
    CHECK(near(a.m[0][3], 1) && near(a.m[1][3], 1.2) && near(a.m[2][3], -2));  // position kept
    const double l = std::sqrt(8.0);
    CHECK(nearVec(col(a, 2), {2 / l, 0, 2 / l}));
    CHECK(nearVec(col(a, 1), {0, 1, 0}));
    CHECK(near(col(a, 0).y, 0));

    // Target above and in front: z tilts up, x stays horizontal, y stays on the upper side
    const DetachedPose b = faceTowards(start, 1, 2.2, -1);
    CHECK(isRotation(toHmdMatrix(b)));
    CHECK(near(col(b, 2).y, std::sqrt(0.5)) && near(col(b, 2).z, std::sqrt(0.5)));
    CHECK(near(col(b, 0).y, 0) && col(b, 1).y > 0);

    // Straight above or below: keeps its x axis, stays a rotation
    DetachedPose turned = start;
    turned = faceTowards(turned, 4, 1.2, -2);  // face +x first
    const DetachedPose above = faceTowards(turned, 1, 5, -2);
    CHECK(isRotation(toHmdMatrix(above)));
    CHECK(nearVec(col(above, 2), {0, 1, 0}));
    CHECK(near(len(col(above, 0)), 1));
    const DetachedPose below = faceTowards(turned, 1, -5, -2);
    CHECK(isRotation(toHmdMatrix(below)));
    CHECK(nearVec(col(below, 2), {0, -1, 0}));

    // A point on top of it: unchanged
    const DetachedPose same = faceTowards(turned, turned.m[0][3], turned.m[1][3], turned.m[2][3]);
    CHECK(same == turned);

    // Roundtrip with placeInFront: the panel is already facing the HMD, so face-me changes nothing
    const vr::HmdMatrix34_t hmd = anyPose(-70, 0, 0, 0.5, 1.6, 0.5);
    const DetachedPose front = placeInFront(hmd);
    const DetachedPose again = faceTowards(front, 0.5, 1.6, 0.5);
    CHECK(nearPose(front, again, 1e-3));
}

void testNudge() {
    // Rotated about y by 90°: the panel's right (x) is world −z, its front (z) is world +x
    DetachedPose p = fromHmdMatrix(yawPose(90, 1, 1.5, -1));
    CHECK(nearVec(col(p, 0), {0, 0, -1}) && nearVec(col(p, 2), {1, 0, 0}));
    const DetachedPose right = nudgeDetached(p, 0, 0.1);
    CHECK(near(right.m[0][3], 1) && near(right.m[1][3], 1.5) && near(right.m[2][3], -1.1));
    const DetachedPose up = nudgeDetached(p, 1, 0.05);
    CHECK(near(up.m[0][3], 1) && near(up.m[1][3], 1.55) && near(up.m[2][3], -1));
    const DetachedPose near_ = nudgeDetached(p, 2, kDetachedDepthStepM);  // towards the viewer = along +z of the panel
    CHECK(near(near_.m[0][3], 1.05) && near(near_.m[1][3], 1.5) && near(near_.m[2][3], -1));
    const DetachedPose far_ = nudgeDetached(p, 2, -kDetachedDepthStepM);
    CHECK(near(far_.m[0][3], 0.95));
    // Rotation untouched, valid kept
    CHECK(right.valid && nearVec(col(right, 0), col(p, 0)) && nearVec(col(right, 2), col(p, 2)));
    // Nudge and back is the identity
    CHECK(nearPose(nudgeDetached(nudgeDetached(p, 0, kDetachedNudgeM), 0, -kDetachedNudgeM), p, 1e-9));
    // Tilted panel: "up" follows the panel, not the world
    const DetachedPose tilted = fromHmdMatrix(anyPose(0, 45, 0, 0, 1, 0));
    const DetachedPose tUp = nudgeDetached(tilted, 1, 1.0);
    CHECK(nearVec({tUp.m[0][3], tUp.m[1][3] - 1, tUp.m[2][3]}, col(tilted, 1)));
}

/** @return the rotation panelRotation builds, as a matrix */
vr::HmdMatrix34_t rotationOf(double yaw, double pitch, double roll) {
    return anyPose(yaw, pitch, roll, 0, 0, 0);
}

void testRotate() {
    const DetachedPose id = fromHmdMatrix(identityMatrix());
    // Same sign conventions as the head panel: yaw + / pitch + / roll + = panelRotation's
    for (const double a : {5.0, 30.0, -20.0, 90.0}) {
        vr::HmdMatrix34_t m = toHmdMatrix(rotateDetached(id, 1, a));
        CHECK(nearMatrix(m, rotationOf(a, 0, 0)));
        m = toHmdMatrix(rotateDetached(id, 0, a));
        CHECK(nearMatrix(m, rotationOf(0, a, 0)));
        m = toHmdMatrix(rotateDetached(id, 2, a));
        CHECK(nearMatrix(m, rotationOf(0, 0, a)));
    }
    // What the signs mean: yaw + turns the face towards +x, pitch + towards +y, roll + counter-clockwise from the front
    CHECK(col(rotateDetached(id, 1, 10), 2).x > 0);
    CHECK(col(rotateDetached(id, 0, 10), 2).y > 0);
    CHECK(col(rotateDetached(id, 2, 10), 0).y > 0);
    CHECK(col(rotateDetached(id, 1, -10), 2).x < 0);

    // In place: the position stays, valid stays
    DetachedPose p = fromHmdMatrix(anyPose(33, -12, 7, 0.4, 1.3, -0.9));
    for (int axis = 0; axis < 3; ++axis) {
        const DetachedPose r = rotateDetached(p, axis, 15);
        CHECK(r.valid && isRotation(toHmdMatrix(r)));
        CHECK(near(r.m[0][3], 0.4, 1e-6) && near(r.m[1][3], 1.3, 1e-6) && near(r.m[2][3], -0.9, 1e-6));
        // Turned about the panel's own axis: that axis doesn't move
        const int fixedColumn = axis == 0 ? 0 : (axis == 1 ? 1 : 2);
        CHECK(nearVec(col(r, fixedColumn), col(p, fixedColumn), 1e-3));
        // ... by the right angle: the other two axes turned 15° about it
        const int other = axis == 0 ? 1 : (axis == 1 ? 2 : 0);
        CHECK(near(dot(col(r, other), col(p, other)), std::cos(15 * M_PI / 180), 1e-3));
        // Back again
        CHECK(nearPose(rotateDetached(r, axis, -15), p, 1e-3));
    }
    // The pose rotation is the composition of the old one and panelRotation's (panel's own axes)
    {
        const DetachedPose r = rotateDetached(p, 1, 25);
        vr::HmdMatrix34_t local = rotationOf(25, 0, 0);
        CHECK(nearMatrix(toHmdMatrix(r), composeTransform(toHmdMatrix(p), local), 1e-3));
    }
}

// ---- Config ----

std::string tempDir;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void writeFile(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

bool anyWarningMentions(const std::vector<std::string>& warnings, const std::string& what) {
    for (const std::string& w : warnings) {
        if (w.find(what) != std::string::npos) return true;
    }
    return false;
}

void testConfigRoundTrip() {
    const std::string path = tempDir + "/roundtrip.json";
    Config c;
    c.attachment = Attachment::Detached;
    c.detachedPose = fromHmdMatrix(anyPose(35, -14, 9, 0.123456789, 1.23456789, -2.3456789));
    std::string error;
    CHECK(saveConfig(path, c, error));
    const std::string text = readFile(path);
    CHECK(text.find("\"attachment\": \"detached\"") != std::string::npos);
    CHECK(text.find("\"detached_pose\": [") != std::string::npos);

    Config back;
    std::vector<std::string> warnings;
    CHECK(loadConfig(path, back, warnings, error));
    CHECK(back.attachment == Attachment::Detached);
    CHECK(back.detachedPose.valid);
    CHECK(!anyWarningMentions(warnings, "detached"));
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 4; ++k) {
            // 6 significant digits: relative error below 1e-5
            const double want = c.detachedPose.m[r][k];
            CHECK(std::fabs(back.detachedPose.m[r][k] - want) <= 1e-5 * std::max(1.0, std::fabs(want)));
        }
    }
    CHECK(isRotation(toHmdMatrix(back.detachedPose), 1e-4));

    // Saved again, a second load gives the same 6-digit values (no drift)
    const std::string path2 = tempDir + "/roundtrip2.json";
    CHECK(saveConfig(path2, back, error));
    Config again;
    warnings.clear();
    CHECK(loadConfig(path2, again, warnings, error));
    CHECK(again.detachedPose == back.detachedPose);

    // Exactly 6 significant digits on disk
    const std::string path3 = tempDir + "/digits.json";
    Config digits;
    digits.detachedPose.valid = true;
    digits.detachedPose.m[0][3] = 0.123456789;
    digits.detachedPose.m[1][3] = -1234.56789;
    digits.detachedPose.m[2][3] = -0.0;
    CHECK(saveConfig(path3, digits, error));
    const std::string t3 = readFile(path3);
    CHECK(t3.find("0.123457") != std::string::npos);
    CHECK(t3.find("-1234.57") != std::string::npos);
    CHECK(t3.find("-0,") == std::string::npos && t3.find("-0]") == std::string::npos);  // negative zero is written as 0

    // Every attachment name round-trips
    for (const Attachment a : {Attachment::Head, Attachment::LeftWrist, Attachment::RightWrist, Attachment::Detached}) {
        Config x;
        x.attachment = a;
        CHECK(saveConfig(path, x, error));
        Config y;
        warnings.clear();
        CHECK(loadConfig(path, y, warnings, error));
        CHECK(y.attachment == a);
        CHECK(std::string(attachmentName(a)).size() > 0);
    }
    CHECK(std::string(attachmentName(Attachment::Detached)) == "detached");
}

void testConfigNoPoseNoKey() {
    const std::string path = tempDir + "/nopose.json";
    Config c;
    c.attachment = Attachment::Detached;  // chosen but never placed
    CHECK(!c.detachedPose.valid);
    std::string error;
    CHECK(saveConfig(path, c, error));
    CHECK(readFile(path).find("detached_pose") == std::string::npos);
    Config back;
    std::vector<std::string> warnings;
    CHECK(loadConfig(path, back, warnings, error));
    CHECK(back.attachment == Attachment::Detached);
    CHECK(!back.detachedPose.valid);
    CHECK(!anyWarningMentions(warnings, "detached"));

    // A missing file: defaults, no pose
    Config none;
    CHECK(loadConfig(tempDir + "/does-not-exist.json", none, warnings, error));
    CHECK(!none.detachedPose.valid && none.attachment == Attachment::Head);
}

void testConfigMalformedPose() {
    const std::string cases[] = {
        "[1,0,0,0, 0,1,0,0, 0,0,1]",                  // 11 numbers
        "[1,0,0,0, 0,1,0,0, 0,0,1,0, 0]",             // 13 numbers
        "[]",                                         // empty
        "[1,0,0,0, 0,1,0,0, 0,0,\"x\",0]",            // a string
        "[1,0,0,0, 0,1,0,0, 0,0,null,0]",             // null
        "[1,0,0,0, 0,1,0,0, 0,0,true,0]",             // bool
        "[[1,0,0,0],[0,1,0,0],[0,0,1,0]]",            // nested 3x4
        "\"1,0,0,0\"",                                // a string, not an array
        "{\"m\": 1}",                                 // an object
        "12",                                         // a number
        "null",
    };
    int n = 0;
    for (const std::string& value : cases) {
        const std::string path = tempDir + "/bad" + std::to_string(n++) + ".json";
        writeFile(path, "{\n  \"attachment\": \"detached\",\n  \"detached_pose\": " + value + "\n}\n");
        Config c;
        std::vector<std::string> warnings;
        std::string error;
        CHECK(loadConfig(path, c, warnings, error));  // the file still loads
        CHECK(!c.detachedPose.valid);
        CHECK(anyWarningMentions(warnings, "detached_pose"));
        CHECK(c.attachment == Attachment::Detached);  // the rest of the file is honoured
        if (c.detachedPose.valid || !anyWarningMentions(warnings, "detached_pose")) {
            std::printf("  (case %d: %s)\n", n - 1, value.c_str());
        }
    }
    // Good one for contrast: the same shape with 12 numbers
    const std::string good = tempDir + "/good.json";
    writeFile(good, "{ \"detached_pose\": [1,0,0,0.5, 0,1,0,1.5, 0,0,1,-2] }");
    Config c;
    std::vector<std::string> warnings;
    std::string error;
    CHECK(loadConfig(good, c, warnings, error));
    CHECK(c.detachedPose.valid && c.detachedPose.m[0][3] == 0.5 && c.detachedPose.m[1][3] == 1.5 && c.detachedPose.m[2][3] == -2);
    CHECK(!anyWarningMentions(warnings, "detached_pose"));

    // A bad attachment name warns and keeps the head
    const std::string badName = tempDir + "/badname.json";
    writeFile(badName, "{ \"attachment\": \"floating\" }");
    Config d;
    warnings.clear();
    CHECK(loadConfig(badName, d, warnings, error));
    CHECK(d.attachment == Attachment::Head);
    CHECK(anyWarningMentions(warnings, "attachment"));
}

// the Detached curve in degrees, and the detached_curve_deg config key
void testCurveSteps() {
    CHECK(kDetachedCurveMaxDeg == 90 && kDetachedCurveStepDeg == 10);
    const struct { double in; int want; } clampCases[] = {{-5, 0}, {std::nan(""), 0}, {0, 0}, {4, 0}, {5, 10}, {34, 30},
                                                          {35, 40}, {90, 90}, {200, 90}};
    for (const auto& k : clampCases) CHECK(clampCurveDeg(k.in) == k.want);

    CHECK(detachedCurvature(0) == 0.0f);
    CHECK(detachedCurvature(40) == static_cast<float>(40.0 / 360.0));
    CHECK(detachedCurvature(90) == 0.25f);
    CHECK(detachedCurvature(100) == 0.25f);  // clamps
    CHECK(detachedCurvature(-10) == 0.0f);
    // A 0.4 m panel at 90° wraps a circle of radius W / θ = 0.4 / (π/2); the same formula as camtiles::curvatureRadius
    // (tested against 0.25 in camera_tile_geometry_test, which keeps grid_renderer.h out of this test)
    const double r = 0.4 / (2.0 * M_PI * detachedCurvature(90));
    CHECK(std::fabs(r - 0.4 / (M_PI / 2)) < 1e-5);
    CHECK(std::fabs(r - 0.2546) < 1e-4);
}

void testConfigCurve() {
    std::string error;
    std::vector<std::string> warnings;
    CHECK(Config().detachedCurveDeg == 0);

    // Round trip of every step, no warnings
    for (int deg = 0; deg <= 90; deg += 10) {
        const std::string path = tempDir + "/curve.json";
        Config c;
        c.detachedCurveDeg = deg;
        CHECK(saveConfig(path, c, error));
        // written only when not 0 (a config that never curved the panel saves as before)
        CHECK((readFile(path).find("\"detached_curve_deg\": " + std::to_string(deg)) != std::string::npos) == (deg != 0));
        Config back;
        back.detachedCurveDeg = 50;  // must be overwritten
        warnings.clear();
        CHECK(loadConfig(path, back, warnings, error));
        CHECK(back.detachedCurveDeg == deg);
        CHECK(!anyWarningMentions(warnings, "detached_curve"));
        CHECK(!anyWarningMentions(warnings, "unknown"));
    }
    // Rounded to 10 and clamped to 0..90 on load; out-of-range warns
    const struct { const char* text; int want; bool warns; } cases[] = {
        {"37", 40, false}, {"34", 30, false}, {"35", 40, false}, {"200", 90, true}, {"-10", 0, true}, {"90", 90, false}, {"0", 0, false}};
    int n = 0;
    for (const auto& k : cases) {
        const std::string path = tempDir + "/curveclamp" + std::to_string(n++) + ".json";
        writeFile(path, std::string("{ \"detached_curve_deg\": ") + k.text + " }");
        Config c;
        warnings.clear();
        CHECK(loadConfig(path, c, warnings, error));
        CHECK(c.detachedCurveDeg == k.want);
        CHECK(anyWarningMentions(warnings, "detached_curve_deg") == k.warns);
    }
    // Missing key: 0, no warning
    {
        const std::string path = tempDir + "/curvemissing.json";
        writeFile(path, "{ \"attachment\": \"detached\" }");
        Config fresh;
        warnings.clear();
        CHECK(loadConfig(path, fresh, warnings, error));
        CHECK(!anyWarningMentions(warnings, "detached_curve"));
        CHECK(fresh.detachedCurveDeg == 0);
    }
    // resetDisplaySettings
    {
        Config c;
        c.detachedCurveDeg = 60;
        resetDisplaySettings(c);
        CHECK(c.detachedCurveDeg == 0);
    }
}

// A config from before Room existed (v0.4.0 shape: no detached_* keys) loads quietly and saves without them
void testOldConfigStaysClean() {
    std::string error;
    std::vector<std::string> warnings;
    const std::string hand = tempDir + "/v040.json";
    writeFile(hand,
              "{\n  \"visible\": true,\n  \"attachment\": \"right_wrist\",\n  \"width_m\": 0.4,\n  \"alpha\": 0.9,\n"
              "  \"position\": { \"x\": 0.0, \"y\": -0.1, \"z\": -0.5 },\n  \"clock_format\": 24\n}\n");
    for (const Attachment a : {Attachment::Head, Attachment::LeftWrist, Attachment::RightWrist}) {
        Config c;
        c.attachment = a;
        const std::string first = tempDir + "/old1.json";
        const std::string second = tempDir + "/old2.json";
        CHECK(saveConfig(first, c, error));  // what a config that never used Room looks like on disk
        Config back;
        warnings.clear();
        CHECK(loadConfig(first, back, warnings, error));
        CHECK(warnings.empty());
        CHECK(back.attachment == a && !back.detachedPose.valid && back.detachedCurveDeg == 0);
        CHECK(saveConfig(second, back, error));
        CHECK(readFile(first) == readFile(second));  // load -> save is stable
        CHECK(readFile(first).find("detached_pose") == std::string::npos);
        CHECK(readFile(first).find("detached_curve_deg") == std::string::npos);
    }
    Config c;
    warnings.clear();
    CHECK(loadConfig(hand, c, warnings, error));
    CHECK(warnings.empty());
    CHECK(c.attachment == Attachment::RightWrist && !c.detachedPose.valid && c.detachedCurveDeg == 0);
    const std::string out = tempDir + "/v040-out.json";
    CHECK(saveConfig(out, c, error));
    CHECK(readFile(out).find("detached_") == std::string::npos);
}

// Picking Room then going back keeps the pose (it was used); Reset drops the curve and the attachment, keeps the pose
void testRoomThenBackAndReset() {
    std::string error;
    std::vector<std::string> warnings;
    Config c;
    c.attachment = Attachment::Detached;
    c.detachedPose = fromHmdMatrix(anyPose(20, 0, 0, 0.5, 1.4, -1));
    c.detachedCurveDeg = 40;
    c.attachment = Attachment::Head;
    const std::string path = tempDir + "/back.json";
    CHECK(saveConfig(path, c, error));
    const std::string text = readFile(path);
    CHECK(text.find("\"attachment\": \"head\"") != std::string::npos);
    CHECK(text.find("\"detached_pose\": [") != std::string::npos);
    CHECK(text.find("\"detached_curve_deg\": 40") != std::string::npos);

    c.attachment = Attachment::Detached;
    const DetachedPose pose = c.detachedPose;
    resetDisplaySettings(c);
    CHECK(c.attachment == Attachment::Head);
    CHECK(c.detachedCurveDeg == 0);
    CHECK(c.detachedPose == pose);
    const std::string path2 = tempDir + "/reset.json";
    CHECK(saveConfig(path2, c, error));
    CHECK(readFile(path2).find("detached_pose") != std::string::npos);
    CHECK(readFile(path2).find("detached_curve_deg") == std::string::npos);
    Config back;
    CHECK(loadConfig(path2, back, warnings, error));
    CHECK(back.detachedPose.valid && back.detachedCurveDeg == 0 && back.attachment == Attachment::Head);
}

}  // namespace

int main() {
    char pattern[] = "/tmp/frame-perf-overlay-detached-test-XXXXXX";
    const char* dir = mkdtemp(pattern);
    if (dir == nullptr) {
        std::printf("mkdtemp failed\n");
        return 1;
    }
    tempDir = dir;

    testConvert();
    testInvertRigid();
    testOrthonormalize();
    testGrabKeepsThePanelRigid();
    testStepGrab();
    testStepGrabLaserAndActions();
    testPlaceInFront();
    testFaceTowards();
    testNudge();
    testRotate();
    testConfigRoundTrip();
    testConfigNoPoseNoKey();
    testConfigMalformedPose();
    testCurveSteps();
    testConfigCurve();
    testOldConfigStaysClean();
    testRoomThenBackAndReset();

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    if (failures == 0) std::printf("detached-test: all passed\n");
    return failures == 0 ? 0 : 1;
}
