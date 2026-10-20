#include "physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cstddef>

namespace s3d {
namespace {

// THERE ARE NO WALLS.
//
// The world has no box around it, and that is the point: a game decides where
// its own bounds are, and a hard-coded box that stops a thing in mid air is
// not a bound - it is a bug with a shape. What falls out of the world keeps
// falling, and what flies out of it keeps flying.
//
// There was a floor once, at y = -9, under a grid drawn at y = 0: a body fell
// through the grid and stopped nine metres below it, in mid air, against
// something nobody could see. The walls went the same way.

// Broadphase cell size in metres.
constexpr float kCellSize = 2.0f;
constexpr int kSolverIterations = 8;

// Contacts are created while the surfaces are still this far apart, so a resting
// body keeps its contact (and therefore its solver impulse) instead of dropping
// in and out every step, which would make it jitter and never fall asleep.
constexpr float kContactMargin = 0.02f;

// Penetration below this is left alone; it is cheaper to let small overlaps
// persist than to push the body out every step.
constexpr float kPenetrationSlop = 0.005f;

// Fraction of the remaining overlap removed per step, for walls and for body
// pairs respectively.
constexpr float kWallCorrection = 0.6f;
constexpr float kPairCorrection = 0.4f;

// --- Oriented-box vs oriented-box (SAT) ------------------------------------
//
// The corner-sampling test this replaces only fired when a corner of one box sat
// STRICTLY inside the other on all three axes (dx>0 && dy>0 && dz>0). Two boxes
// resting flush - the single most common way anything gets built - have their
// corners exactly on the edges, so dx==0 and NO contact was ever produced: the
// upper box fell straight through the lower one. Corner sampling cannot represent
// a face resting on a face at all, which is why a wall of equal cubes collapsed.
//
// This is the real Separating Axis Theorem: test the 15 candidate axes (6 face
// normals, 9 edge cross products), take the axis of least penetration, then build
// a contact manifold by clipping the incident face against the reference face's
// side planes. That yields up to 4 well-spread contact points, correct for
// face-on-face stacks and stable for the warm-start cache.

struct SatPoint {
    Vec3 pos;    // world-space contact point
    Vec3 normal; // contact normal, from B toward A (the solver's convention)
    float depth; // penetration depth along the normal
};

struct BoxFrame {
    Vec3 u[3]; // world axes of the box
    Vec3 c;    // centre
    Vec3 h;    // half extents
};

BoxFrame frameOf(const RigidBody& b) {
    BoxFrame f;
    f.u[0] = rotate(b.orientation, Vec3{1.0f, 0.0f, 0.0f});
    f.u[1] = rotate(b.orientation, Vec3{0.0f, 1.0f, 0.0f});
    f.u[2] = rotate(b.orientation, Vec3{0.0f, 0.0f, 1.0f});
    f.c = b.position;
    f.h = b.halfExtents;
    return f;
}

// Corner `i`: bit 0 selects the +/-, likewise for y and z.
Vec3 cornerOf(const BoxFrame& f, int i) {
    return f.c + f.u[0] * (f.h.x * ((i & 1) ? 1.0f : -1.0f)) +
           f.u[1] * (f.h.y * ((i & 2) ? 1.0f : -1.0f)) +
           f.u[2] * (f.h.z * ((i & 4) ? 1.0f : -1.0f));
}

// Clips a polygon against the half-space dot(p - o, n) <= 0, in place.
void clipToPlane(Vec3 poly[8], int& count, const Vec3& o, const Vec3& n) {
    Vec3 out[8];
    int outCount = 0;
    for (int i = 0; i < count; ++i) {
        const Vec3 cur = poly[i];
        const Vec3 nxt = poly[(i + 1) % count];
        const float dCur = dot(cur - o, n);
        const float dNxt = dot(nxt - o, n);
        if (dCur <= 0.0f) out[outCount++] = cur;
        if ((dCur > 0.0f) != (dNxt > 0.0f)) {
            const float t = dCur / (dCur - dNxt);
            out[outCount++] = cur + (nxt - cur) * t;
        }
    }
    count = outCount;
    for (int i = 0; i < outCount; ++i) poly[i] = out[i];
}

// Manifold for A against B. Every point's normal points from B toward A, which is
// the convention the solver expects. Returns the point count, or 0 when separated.
int collideBoxes(const RigidBody& A, const RigidBody& B, SatPoint out[4]) {
    const BoxFrame fa = frameOf(A);
    const BoxFrame fb = frameOf(B);

    // R = fa^T * fb (B's axes in A's frame) and its absolute value for the
    // projection radii. Precomputing these once is what makes 15 axes cheap.
    float R[3][3], AbsR[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            R[i][j] = dot(fa.u[i], fb.u[j]);
            AbsR[i][j] = std::fabs(R[i][j]) + 1e-6f;
        }
    const Vec3 t = B.position - A.position;
    const Vec3 ta{dot(t, fa.u[0]), dot(t, fa.u[1]), dot(t, fa.u[2])};

    // Every axis gives a separation that is negative while the boxes overlap, and
    // positive means "this axis proves they are apart". The axis we want is the
    // one of LEAST penetration - the largest separation, i.e. the one closest to
    // zero. Tracking the smallest value instead picks the deepest overlap, which
    // is usually a side face rather than the face the boxes are resting on; the
    // manifold then gets built against that wrong face and the solver shoves the
    // pair sideways instead of holding it up.
    float bestSep = -1e30f;
    int bestAxis = -1; // 0..2 = A face, 3..5 = B face, 6..14 = edge cross

    for (int i = 0; i < 3; ++i) { // A's face normals
        const float rb = fb.h.x * AbsR[i][0] + fb.h.y * AbsR[i][1] + fb.h.z * AbsR[i][2];
        const float sep = std::fabs(component(ta, i)) - (component(fa.h, i) + rb);
        if (sep > 0.0f) return 0;
        if (sep > bestSep) { bestSep = sep; bestAxis = i; }
    }
    for (int j = 0; j < 3; ++j) { // B's face normals
        const float ra = fa.h.x * AbsR[0][j] + fa.h.y * AbsR[1][j] + fa.h.z * AbsR[2][j];
        const float sep = std::fabs(ta.x * R[0][j] + ta.y * R[1][j] + ta.z * R[2][j]) -
                          (ra + component(fb.h, j));
        if (sep > 0.0f) return 0;
        if (sep > bestSep) { bestSep = sep; bestAxis = 3 + j; }
    }
    for (int i = 0; i < 3; ++i) { // nine edge-edge cross products
        for (int j = 0; j < 3; ++j) {
            const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            const float ra = component(fa.h, i1) * AbsR[i2][j] + component(fa.h, i2) * AbsR[i1][j];
            const float rb = component(fb.h, j1) * AbsR[i][j2] + component(fb.h, j2) * AbsR[i][j1];
            const float sep = std::fabs(component(ta, i2) * R[i1][j] - component(ta, i1) * R[i2][j]) - (ra + rb);
            if (sep > 0.0f) return 0;
            // Near-parallel cross axes are numerically noisy; skip them and let a
            // face axis win instead.
            const float lenSq = 1.0f - R[i][j] * R[i][j];
            if (lenSq < 0.95f * 0.95f) continue;
            if (sep > bestSep) { bestSep = sep; bestAxis = 6 + i * 3 + j; }
        }
    }

    if (bestAxis < 0) return 0;

    // Edge-edge: one contact at the closest points of the two edges.
    if (bestAxis >= 6) {
        const int i = (bestAxis - 6) / 3;
        const int j = (bestAxis - 6) % 3;
        const Vec3 axisA = fa.u[i], axisB = fb.u[j];
        Vec3 pA = fa.c, pB = fb.c;
        const float r = dot(axisA, axisB);
        if (std::fabs(r) < 0.999f) {
            const Vec3 dp = fb.c - fa.c;
            const float dA = (dot(dp, axisA) - r * dot(dp, axisB)) / (1.0f - r * r);
            const float dB = (dot(dp, axisB) - r * dot(dp, axisA)) / (1.0f - r * r);
            pA += axisA * dA;
            pB += axisB * dB;
        }
        Vec3 n = (pA - pB).normalized();
        if (n.lengthSq() < 0.5f) n = cross(axisA, axisB).normalized();
        if (dot(n, A.position - B.position) < 0.0f) n = -n; // point from B toward A
        out[0].pos = (pA + pB) * 0.5f;
        out[0].normal = n;
        out[0].depth = -bestSep;
        return 1;
    }

    // Face contact: reference face + clipped incident face.
    const bool refIsA = bestAxis < 3;
    const BoxFrame& ref = refIsA ? fa : fb;
    const BoxFrame& inc = refIsA ? fb : fa;
    const int refAxis = bestAxis % 3;

    // The reference face is the one FACING the incident box, so its outward normal
    // must point from ref toward inc. Orienting it the other way picks the far
    // face of the reference box: refFaceCentre then lands on the wrong side, every
    // depth comes out with the wrong sign, and the solver pushes the pair apart
    // instead of holding it up - a stack of cubes falls through the floor.
    Vec3 refNormal = ref.u[refAxis];
    if (dot(refNormal, inc.c - ref.c) < 0.0f) refNormal = -refNormal;
    const int ru = (refAxis + 1) % 3, rv = (refAxis + 2) % 3;
    const Vec3 tanU = ref.u[ru], tanV = ref.u[rv];
    const Vec3 refFaceCentre = ref.c + refNormal * component(ref.h, refAxis);

    // Incident face: the face of the incident box whose normal is most
    // anti-parallel to refNormal. The comparison is `d < bestDot`, so the seed has
    // to start above every real dot product. Seeding it at -1 meant the first axis
    // never won on its own merits and the loop only ever replaced it when some
    // later axis happened to be more negative - which, for two boxes lying flat,
    // picked a side face and clipped the wrong polygon.
    int incAxis = 0;
    float bestDot = 2.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = dot(inc.u[i], refNormal);
        if (d < bestDot) { bestDot = d; incAxis = i; }
    }
    // Build the face whose OUTWARD normal is anti-parallel to refNormal. `incAxis`
    // is the axis with the most negative dot product, and the two faces on that
    // axis have outward normals -inc.u[incAxis] (dot positive) and
    // +inc.u[incAxis] (dot negative). So the face we want is the one whose bit is
    // SET exactly when the dot came out negative.
    const int signBit = (dot(inc.u[incAxis], refNormal) < 0.0f) ? (1 << incAxis) : 0;
    const int a1 = (incAxis + 1) % 3, a2 = (incAxis + 2) % 3;
    Vec3 poly[8];
    int count = 0;
    const int c1_arr[4] = {0, 0, 1, 1};
    const int c2_arr[4] = {0, 1, 1, 0};
    for (int k = 0; k < 4; ++k)
        poly[count++] = cornerOf(inc, signBit | (c1_arr[k] ? (1 << a1) : 0) | (c2_arr[k] ? (1 << a2) : 0));

    // Clip against the reference face's four side planes.
    const float hu = component(ref.h, ru), hv = component(ref.h, rv);
    clipToPlane(poly, count, refFaceCentre + tanU * hu, tanU);
    clipToPlane(poly, count, refFaceCentre - tanU * hu, -tanU);
    clipToPlane(poly, count, refFaceCentre + tanV * hv, tanV);
    clipToPlane(poly, count, refFaceCentre - tanV * hv, -tanV);

    // Keep points at or below the reference plane (penetrating, or within a
    // small speculative margin so a fast approach still gets a contact).
    const Vec3 normal = refIsA ? -refNormal : refNormal; // from B toward A
    const float margin = 0.02f;
    int n = 0;
    for (int i = 0; i < count && n < 4; ++i) {
        const float depth = dot(refFaceCentre - poly[i], refNormal);
        if (depth < -margin) continue;
        out[n].pos = poly[i];
        out[n].normal = normal;
        out[n].depth = depth;
        ++n;
    }
    return n;
}

} // namespace

const char* gravityName(GravityAxis axis) {
    switch (axis) {
        case GravityAxis::NegY: return "-Y (down)";
        case GravityAxis::PosY: return "+Y (up)";
        case GravityAxis::NegX: return "-X";
        case GravityAxis::PosX: return "+X";
        case GravityAxis::NegZ: return "-Z";
        case GravityAxis::PosZ: return "+Z";
    }
    return "?";
}

void PhysicsWorld::setGravity(GravityAxis axis) {
    gravityAxis = axis;
    const float g = gravityMagnitude;
    switch (axis) {
        case GravityAxis::NegY: gravity = {0.0f, -g, 0.0f}; break;
        case GravityAxis::PosY: gravity = {0.0f, g, 0.0f}; break;
        case GravityAxis::NegX: gravity = {-g, 0.0f, 0.0f}; break;
        case GravityAxis::PosX: gravity = {g, 0.0f, 0.0f}; break;
        case GravityAxis::NegZ: gravity = {0.0f, 0.0f, -g}; break;
        case GravityAxis::PosZ: gravity = {0.0f, 0.0f, g}; break;
    }
    wakeAll();
}

void PhysicsWorld::clear() {
    bodies.clear();
    // Pair keys are body indices, so with the bodies gone every key is stale.
    clearPairWarm();
}

void PhysicsWorld::clearWarmStartCache() {
    // The pair table is the only warm-start cache there is: the walls are
    // gone and a body's own cache went with them.
    clearPairWarm();
}

void PhysicsWorld::wakeAll() {
    // Waking every body invalidates every cached impulse with it: an impulse
    // accumulated against a body that is being moved again would be applied to a
    // state it was never measured for.
    clearPairWarm();
    for (RigidBody& b : bodies) b.wake();
}

int PhysicsWorld::sleepingCount() const {
    int n = 0;
    for (const RigidBody& b : bodies)
        if (b.asleep) ++n;
    return n;
}

Vec3 PhysicsWorld::upDirection() const { return -gravity.normalized(); }

RigidBody* PhysicsWorld::find(int index) {
    if (index < 0 || index >= (int)bodies.size()) return nullptr;
    return &bodies[index];
}

const RigidBody* PhysicsWorld::find(int index) const {
    if (index < 0 || index >= (int)bodies.size()) return nullptr;
    return &bodies[index];
}

void PhysicsWorld::remove(int index) {
    if (index < 0 || index >= (int)bodies.size()) return;
    bodies.erase(bodies.begin() + index);
    // Removing a body shifts every index above it, so each stored key now names a
    // different pair than it did. Dropping the table is cheaper than renumbering
    // it and costs one step of warm start at most.
    clearPairWarm();
}

int PhysicsWorld::findActiveCamera() const {
    for (size_t i = 0; i < bodies.size(); ++i)
        if (bodies[i].activeCamera && bodies[i].isCamera()) return (int)i;
    return -1;
}

int PhysicsWorld::findFirstCamera() const {
    for (size_t i = 0; i < bodies.size(); ++i)
        if (bodies[i].isCamera()) return (int)i;
    return -1;
}

void PhysicsWorld::setActiveCamera(int index) {
    // Only one camera is ever the active one, so the flag is cleared everywhere
    // first and set on one body after. Clearing it on the way out is what makes
    // the viewport go back to the free camera when the active camera is
    // deleted: the body that carried the flag is gone, and nothing else has it.
    for (RigidBody& b : bodies) b.activeCamera = false;
    if (index < 0 || index >= (int)bodies.size()) return;
    if (!bodies[(size_t)index].isCamera()) return;
    bodies[(size_t)index].activeCamera = true;
}

void PhysicsWorld::refreshDerived(RigidBody& body) const {
    const Vec3& he = body.halfExtents;
    Vec3 mn{1e30f, 1e30f, 1e30f};
    Vec3 mx{-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
        const Vec3 local{(k & 1) ? he.x : -he.x,
                         (k & 2) ? he.y : -he.y,
                         (k & 4) ? he.z : -he.z};
        const Vec3 p = body.position + rotate(body.orientation, local);
        body.corners[k] = p;
        mn.x = std::fmin(mn.x, p.x);
        mn.y = std::fmin(mn.y, p.y);
        mn.z = std::fmin(mn.z, p.z);
        mx.x = std::fmax(mx.x, p.x);
        mx.y = std::fmax(mx.y, p.y);
        mx.z = std::fmax(mx.z, p.z);
    }
    body.aabbMin = mn;
    body.aabbMax = mx;
}

void PhysicsWorld::step(float dt) {
    integrateForces(dt);

    contacts_.clear();
    if (contacts_.capacity() < bodies.size() * 8)
        contacts_.reserve(bodies.size() * 8 + 64);
    generateContacts();

currentStepDt_ = dt;
    applyWarmStart();
    for (int i = 0; i < kSolverIterations; ++i)
        if (!solveIteration()) break;
    storeWarmStart();

    for (RigidBody& b : bodies) {
        // Saved before the integration so the renderer can interpolate between
        // the state before this step and the state after it. Bodies that did not
        // move have prev == curr, and the interpolation is a no-op for them.
        //
        // Unconditional, and before the sleeping/static test rather than after it:
        // a body that skips the integration is exactly the body whose prev would
        // otherwise keep the value it had when it last moved, which is how a
        // static body ends up drawn sliding back to wherever it used to be.
        b.syncPrevTransform();
        // A camera is placed, not simulated: it does not fall, it does not
        // drift and it does not spin. It is still anchored, though - a camera
        // that follows a moving body is the whole point of having a camera in
        // the scene.
        if (b.asleep) continue;
        if (!b.isCamera()) {
            if (b.material.isStatic) continue;
            b.position += b.velocity * dt;
            b.orientation = integrate(b.orientation, b.angularVelocity, dt);
        }

        if (b.anchorPosition) {
            const Vec3 delta = b.position - b.anchorPoint;
            b.velocity -= delta * (b.anchorSpring * dt);
            b.position = b.anchorPoint + delta * std::fmax(0.0f, 1.0f - b.anchorSpring * dt);
        }
        if (b.anchorRotation) {
            const Quat delta = b.orientation * b.anchorOrientation.conjugate();
            b.angularVelocity -= Vec3{delta.x, delta.y, delta.z} * (b.anchorSpring * dt);
        }
    }

    resolvePenetrations();
    updateSleep(dt);

    // This step's results become next step's seeds. Deliberately last: the
    // penetration pass above indexes pairWarmNextKeys_ by the pairIndex the
    // contacts were built with, so swapping first would hand it the PREVIOUS
    // step's key table and correct the wrong pairs - which is what let boxes sink
    // a little further every frame instead of being pushed back out.
    pairWarmKeys_.swap(pairWarmNextKeys_);
    pairWarmValues_.swap(pairWarmNextValues_);
}

void PhysicsWorld::integrateForces(float dt) {
    for (RigidBody& b : bodies) {
        b.invInertiaCached = b.invInertiaWorld();
        refreshDerived(b);
        b.touching = false;
        if (b.asleep) continue;
        // A camera is a scene object: it is never pushed around by the world.
        if (!b.material.isStatic && !b.isCamera()) {
            b.force += gravity * b.mass;
            b.velocity += b.force * b.invMass * dt;
            b.angularVelocity += b.invInertiaCached * (b.torque * dt);
        }
        b.force = Vec3{};
        b.torque = Vec3{};
    }
}

void PhysicsWorld::generateContacts() {
    collectPairContacts();
}

int PhysicsWorld::seedPairWarm(uint64_t key, size_t& cursor) {
    // Both key arrays are sorted by the same canonical key and walked in the same
    // order, so this is a single forward cursor, not a lookup.
    while (cursor < pairWarmKeys_.size() && pairWarmKeys_[cursor] < key) ++cursor;

    const int index = (int)pairWarmNextKeys_.size();
    pairWarmNextKeys_.push_back(key);
    const size_t row = (size_t)index * (size_t)kPairSlots;
    pairWarmNextValues_.resize(pairWarmNextKeys_.size() * (size_t)kPairSlots);

    if (cursor < pairWarmKeys_.size() && pairWarmKeys_[cursor] == key) {
        const size_t prev = cursor * (size_t)kPairSlots;
        for (int s = 0; s < kPairSlots; ++s)
            pairWarmNextValues_[row + (size_t)s] = pairWarmValues_[prev + (size_t)s];
    }
    // No previous row (new or re-separated pair): resize already zeroed the row,
    // which is exactly the cold start a fresh contact should get.
    return index;
}

void PhysicsWorld::clearPairWarm() {
    pairWarmKeys_.clear();
    pairWarmValues_.clear();
    pairWarmNextKeys_.clear();
    pairWarmNextValues_.clear();
}

void PhysicsWorld::collectPairContacts() {
    cellEntries_.clear();
    pairKeys_.clear();
    const float invCell = 1.0f / kCellSize;

    auto cellKey = [](int cx, int cy, int cz) -> uint64_t {
        return (uint64_t)(uint32_t)(cx + 4096) |
               ((uint64_t)(uint32_t)(cy + 4096) << 13) |
               ((uint64_t)(uint32_t)(cz + 4096) << 26);
    };
    auto pairKey = [](uint32_t a, uint32_t b) -> uint64_t {
        return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
    };
    auto cellRange = [&](const RigidBody& b, int out[6]) {
        out[0] = (int)std::floor(b.aabbMin.x * invCell);
        out[1] = (int)std::floor(b.aabbMax.x * invCell);
        out[2] = (int)std::floor(b.aabbMin.y * invCell);
        out[3] = (int)std::floor(b.aabbMax.y * invCell);
        out[4] = (int)std::floor(b.aabbMin.z * invCell);
        out[5] = (int)std::floor(b.aabbMax.z * invCell);
    };

    int cell[6];
    for (uint32_t bi = 0; bi < bodies.size(); ++bi) {
        RigidBody& b = bodies[bi];
        // A camera is not in the world at all: it is not inserted, so it can
        // never be found as a collision partner. This is the one place that
        // decides what collides with what, which is why it is here and not
        // scattered through the contact code.
        if (b.isCamera()) continue;
        // Static bodies are inserted too. They never drive a query (see the loop
        // below) but they must be findable as collision partners, or an immovable
        // body collides with nothing at all and everything slides straight through
        // it to the arena floor.
        cellRange(b, cell);
        for (int x = cell[0]; x <= cell[1]; ++x)
            for (int y = cell[2]; y <= cell[3]; ++y)
                for (int z = cell[4]; z <= cell[5]; ++z)
                    cellEntries_.push_back({cellKey(x, y, z), bi});
    }

    // Sorting the (cell, body) pairs once lets every query use lower_bound
    // instead of a hash map - no per-step allocation, cache friendly scan.
    std::sort(cellEntries_.begin(), cellEntries_.end(),
              [](const std::pair<uint64_t, uint32_t>& a,
                 const std::pair<uint64_t, uint32_t>& b) { return a.first < b.first; });

    for (uint32_t bi = 0; bi < bodies.size(); ++bi) {
        RigidBody& b = bodies[bi];
        if (b.material.isStatic) continue;
        cellRange(b, cell);
        for (int x = cell[0]; x <= cell[1]; ++x)
            for (int y = cell[2]; y <= cell[3]; ++y)
                for (int z = cell[4]; z <= cell[5]; ++z) {
                    const uint64_t key = cellKey(x, y, z);
                    auto it = std::lower_bound(cellEntries_.begin(), cellEntries_.end(), key,
                        [](const std::pair<uint64_t, uint32_t>& e, uint64_t k) {
                            return e.first < k;
                        });
for (; it != cellEntries_.end() && it->first == key; ++it) {
                        const uint32_t other = it->second;
                        if (other == bi) continue;
                        // Dynamic-dynamic pairs are seen from both sides, so only
                        // the lower index reports them. A static partner never
                        // drives a query of its own, so it must be reported by
                        // whichever dynamic body finds it - skipping it on an index
                        // comparison would drop a static floor with a low index.
                        // Duplicates from a body sharing several cells are removed
                        // by the sort+unique below.
                        if (!bodies[other].material.isStatic && other < bi) continue;
                        pairKeys_.push_back(pairKey(bi, other));
                    }
                }
    }

std::sort(pairKeys_.begin(), pairKeys_.end());
    pairKeys_.erase(std::unique(pairKeys_.begin(), pairKeys_.end()), pairKeys_.end());

    // Reset this step's output table and set up the merge cursor against last
    // step's. Seeding happens inside the loop via seedPairWarm().
    pairWarmNextKeys_.clear();
    pairWarmNextValues_.clear();
    size_t warmCursor = 0;

    for (uint64_t pk : pairKeys_) {
        const uint32_t iA = (uint32_t)(pk >> 32);
        const uint32_t iB = (uint32_t)(pk & 0xffffffffu);
        RigidBody& A = bodies[iA];
        RigidBody& B = bodies[iB];
        if (A.asleep && B.asleep) continue;
        if (A.material.isStatic && B.material.isStatic) continue;

        const Vec3 d = A.position - B.position;
        const float rr = A.boundRadius + B.boundRadius;
        if (d.lengthSq() > rr * rr) continue;

const bool aSphere = A.shape == Shape::Sphere;
        const bool bSphere = B.shape == Shape::Sphere;

        // Claim this pair's row in the next-step table, seeded with whatever the
        // same pair carried last step. A pair that was not in contact then starts
        // from zero.
        const int pairIndex = seedPairWarm(pk, warmCursor);

        if (aSphere && bSphere) {
            const float rA = A.halfExtents.x, rB = B.halfExtents.x;
            const float dist = d.length();
            if (dist < 1e-6f || dist >= rA + rB) continue;
            const Vec3 n = d / dist;
            A.touching = B.touching = true;
            contacts_.push_back({iA, iB, n * rA, n * (-rB), n, rA + rB - dist,
                                 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                 pairIndex});
            continue;
        }

        if (aSphere || bSphere) {
            RigidBody& sphere = aSphere ? A : B;
            RigidBody& box = aSphere ? B : A;
            const float r = sphere.halfExtents.x;
            const Quat inv = box.orientation.conjugate();
            const Vec3 local = rotate(inv, sphere.position - box.position);
            const Vec3& he = box.halfExtents;
            const Vec3 closest{clampf(local.x, -he.x, he.x),
                               clampf(local.y, -he.y, he.y),
                               clampf(local.z, -he.z, he.z)};
            const Vec3 delta = local - closest;
            const float distSq = delta.lengthSq();

            Vec3 localNormal;
            float penetration;
            if (distSq > 1e-12f) {
                const float dist = std::sqrt(distSq);
                if (dist >= r) continue;
                localNormal = delta / dist;
                penetration = r - dist;
            } else {
                // Sphere centre inside the box: escape along the shallowest axis.
                const float dx = he.x - std::fabs(local.x);
                const float dy = he.y - std::fabs(local.y);
                const float dz = he.z - std::fabs(local.z);
                float shallowest = dx;
                int axis = 0;
                float sign = local.x >= 0.0f ? 1.0f : -1.0f;
                if (dy < shallowest) {
                    shallowest = dy;
                    axis = 1;
                    sign = local.y >= 0.0f ? 1.0f : -1.0f;
                }
                if (dz < shallowest) {
                    shallowest = dz;
                    axis = 2;
                    sign = local.z >= 0.0f ? 1.0f : -1.0f;
                }
                localNormal = Vec3{};
                if (axis == 0) localNormal.x = sign;
                else if (axis == 1) localNormal.y = sign;
                else localNormal.z = sign;
                penetration = shallowest + r;
            }

            Vec3 normal = rotate(box.orientation, localNormal);
            const Vec3 worldContact = box.position + rotate(box.orientation, closest);
            if (!aSphere) normal = -normal;

            A.touching = B.touching = true;
            const uint32_t cA = aSphere ? iA : iB;
            const uint32_t cB = aSphere ? iB : iA;
            const Vec3 armSphere = worldContact - sphere.position;
            const Vec3 armBox = worldContact - box.position;
            const Vec3 armA = aSphere ? armSphere : armBox;
            const Vec3 armB = aSphere ? armBox : armSphere;
contacts_.push_back({cA, cB, armA, armB, normal, penetration,
                                 0, 0.0f, 0.0f, 0.0f, 0.0f,
                                 pairIndex});
            continue;
        }

        // Box vs box: real SAT with a clipped contact manifold. The manifold
        // point index doubles as the warm-start slot (0..3), which is stable for
        // a resting face contact - exactly the case warm starting exists for.
        SatPoint manifold[4];
        const int points = collideBoxes(A, B, manifold);
        for (int p = 0; p < points; ++p) {
            const Vec3& cp = manifold[p].pos;
            A.touching = B.touching = true;
            contacts_.push_back({iA, iB, cp - A.position, cp - B.position,
                                 manifold[p].normal, manifold[p].depth,
                                 p, 0.0f, 0.0f, 0.0f, 0.0f,
                                 pairIndex});
        }
    }
}

void PhysicsWorld::applyWarmStart() {
    for (Contact& c : contacts_) {
        // A contact that still has a gap must not receive an impulse: doing so
        // would launch the body as soon as it re-enters contact.
        if (c.penetration < 0.0f) continue;

        float jn, jt;
        {
            // Read this pair's row, at this contact point's slot. The table was
            // seeded in collectPairContacts() from whatever the same pair carried
            // last step; a pair that just formed has a zeroed row and starts cold.
            jn = 0.0f;
            jt = 0.0f;
            if (c.pairIndex >= 0) {
                const size_t cell = (size_t)c.pairIndex * (size_t)kPairSlots +
                                    (size_t)(c.slot & (kPairSlots - 1));
                if (cell < pairWarmNextValues_.size()) {
                    jn = pairWarmNextValues_[cell].normal;
                    jt = pairWarmNextValues_[cell].tangent;
                }
            }
        }
        if (jn <= 0.0f) continue;

        RigidBody& A = bodies[c.bodyA];
        if (A.material.isStatic) continue;

        const Vec3 pointVel = A.pointVelocity(c.armA);
        const float vn = dot(pointVel, c.normal);
        if (vn >= 0.0f) continue;

        const Vec3 tangentVel = pointVel - c.normal * vn;
        const float speed = tangentVel.length();
        const Vec3 tangent = speed > 1e-4f ? tangentVel / speed : Vec3{};
        A.applyImpulse(c.normal * jn + tangent * jt, c.armA);
        c.accumulatedNormal = jn;
        c.accumulatedTangent = jt;
    }
}

bool PhysicsWorld::solveIteration() {
    constexpr float kRestitution = 0.22f;
    constexpr float kFriction = 0.6f;
    constexpr float kRestitutionThreshold = -1.2f;

    const float invStepTime = 1.0f / std::fmax(1e-6f, currentStepDt_);

    bool applied = false;
    for (Contact& c : contacts_) {
        RigidBody& A = bodies[c.bodyA];
        RigidBody* B = c.bodyB != Contact::kNoBody ? &bodies[c.bodyB] : nullptr;

        const bool aAwake = !A.asleep && !A.material.isStatic;
        const bool bAwake = B && !B->asleep && !B->material.isStatic;
        const float invMassA = aAwake ? A.invMass : 0.0f;
        const float invMassB = bAwake ? B->invMass : 0.0f;
        if (invMassA == 0.0f && invMassB == 0.0f) continue;

        Vec3 vA = aAwake ? A.pointVelocity(c.armA) : Vec3{};
        Vec3 vB = bAwake ? B->pointVelocity(c.armB) : Vec3{};
        Vec3 relative = vA - vB;
const float vn = dot(relative, c.normal);
        if (vn >= 0.0f) continue;

        // Speculative contacts exist only to catch the step in which the
        // remaining gap closes. While the surfaces are still further apart than
        // this step can close, they must stay untouched.
        if (c.penetration < 0.0f && -vn <= -c.penetration * invStepTime) continue;

        const Mat3& inertiaA = A.invInertiaCached;
        static const Mat3 kIdentity;
        const Mat3& inertiaB = B ? B->invInertiaCached : kIdentity;
        const Vec3 rnA = cross(c.armA, c.normal);
        const Vec3 rnB = cross(c.armB, c.normal);
        const Vec3 wA = aAwake ? inertiaA * rnA : Vec3{};
        const Vec3 wB = bAwake ? inertiaB * rnB : Vec3{};
        const float kNormal =
            invMassA + invMassB + dot(c.normal, cross(wA, c.armA)) +
            dot(c.normal, cross(wB, c.armB));
        if (kNormal < 1e-10f) continue;

const float restitution =
            (vn < kRestitutionThreshold && c.accumulatedNormal <= 0.0f) ? kRestitution : 0.0f;
        const float jn = -(1.0f + restitution) * vn / kNormal;
        const Vec3 pNormal = c.normal * jn;
        if (aAwake) {
            A.velocity += pNormal * invMassA;
            A.angularVelocity += inertiaA * cross(c.armA, pNormal);
        }
        if (bAwake) {
            B->velocity -= pNormal * invMassB;
            B->angularVelocity -= inertiaB * cross(c.armB, pNormal);
        }
        c.accumulatedNormal += jn;

        vA = aAwake ? A.pointVelocity(c.armA) : Vec3{};
        vB = bAwake ? B->pointVelocity(c.armB) : Vec3{};
        relative = vA - vB;
const Vec3 tangentVel = relative - c.normal * dot(relative, c.normal);
        const float tangentSpeed = tangentVel.length();
        if (tangentSpeed > 1e-4f) {
            const Vec3 dir = tangentVel / tangentSpeed;
            const Vec3 rtA = cross(c.armA, dir);
            const Vec3 rtB = cross(c.armB, dir);
            const Vec3 wtA = aAwake ? inertiaA * rtA : Vec3{};
            const Vec3 wtB = bAwake ? inertiaB * rtB : Vec3{};
            const float kTangent =
                invMassA + invMassB + dot(dir, cross(wtA, c.armA)) + dot(dir, cross(wtB, c.armB));
            if (kTangent > 1e-10f) {
                float jt = -tangentSpeed / kTangent;
                const float maxFriction = kFriction * std::fabs(c.accumulatedNormal);
                jt = std::clamp(jt, -maxFriction, maxFriction);
                const Vec3 pTangent = dir * jt;
                if (aAwake) {
                    A.velocity += pTangent * invMassA;
                    A.angularVelocity += inertiaA * cross(c.armA, pTangent);
                }
                if (bAwake) {
                    B->velocity -= pTangent * invMassB;
                    B->angularVelocity -= inertiaB * cross(c.armB, pTangent);
                }
                c.accumulatedTangent += jt;
            }
        }
        applied = true;
    }
    return applied;
}

void PhysicsWorld::storeWarmStart() {
    for (Contact& c : contacts_) {
        // Body-body warm start lives in the persistent pair table, not on the
        // contact: the contact is destroyed at the start of the next step, so
        // anything written here would be gone before it was ever read. Each
        // contact point has its own slot, so a pair's points do not overwrite
        // one another.
        if (c.pairIndex < 0) continue;
        const size_t cell = (size_t)c.pairIndex * (size_t)kPairSlots +
                            (size_t)(c.slot & (kPairSlots - 1));
        if (cell >= pairWarmNextValues_.size()) continue;
        pairWarmNextValues_[cell].normal = std::clamp(c.accumulatedNormal, 0.0f, 1e6f);
        const float maxTangent = 0.6f * std::fabs(c.accumulatedNormal);
        pairWarmNextValues_[cell].tangent =
            std::clamp(c.accumulatedTangent, -maxTangent, maxTangent);
    }

    // No cache sweep is needed: applyWarmStart only accepts an impulse whose
    // stamp is from the immediately preceding step, so anything older is inert
    // and costs nothing to skip.
}

void PhysicsWorld::resolvePenetrations() {
    const std::size_t n = bodies.size();
    if (n == 0 || contacts_.empty()) return;

    // Body pairs: resolve each pair ONCE, from its deepest contact point.
    //
    // Doing it per contact is the same bug a per-contact correction has: a
    // box-box pair emits up to 16 contact points, so pushing on every one
    // moves the pair up to 16x further than it needs, overshoots, the next
    // step finds a gap, and the stack never settles. Folding each pair down to
    // its deepest point first makes the correction once-per-pair and
    // order-independent.
    const size_t pairs = pairWarmNextKeys_.size();

    if (pairs > 0) {
        if (pairDeepest_.size() < pairs) {
            pairDeepest_.resize(pairs);
            pairPush_.resize(pairs);
        }
        std::fill(pairDeepest_.begin(), pairDeepest_.begin() + (std::ptrdiff_t)pairs, 0.0f);
        for (const Contact& c : contacts_) {
            if (c.bodyB == Contact::kNoBody || c.pairIndex < 0) continue;
            const size_t p = (size_t)c.pairIndex;
            if (p >= pairs) continue;
            if (c.penetration > pairDeepest_[p]) {
                pairDeepest_[p] = c.penetration;
                pairPush_[p] = c.normal;
            }
        }
        for (size_t p = 0; p < pairs; ++p) {
            const float penetration = pairDeepest_[p];
            if (penetration <= kPenetrationSlop) continue;
            const uint64_t pk = pairWarmNextKeys_[p];
            RigidBody& A = bodies[(uint32_t)(pk >> 32)];
            RigidBody& B = bodies[(uint32_t)(pk & 0xffffffffu)];
            const float invA = (A.asleep || A.material.isStatic) ? 0.0f : A.invMass;
            const float invB = (B.asleep || B.material.isStatic) ? 0.0f : B.invMass;
            const float sum = invA + invB;
            if (sum <= 0.0f) continue;
            const float correction = (penetration - kPenetrationSlop) * kPairCorrection;
            const Vec3 dir = pairPush_[p];
            if (invA > 0.0f) A.position += dir * (correction * invA / sum);
            if (invB > 0.0f) B.position -= dir * (correction * invB / sum);
        }
    }
}

void PhysicsWorld::updateSleep(float dt) {
    constexpr float kLinearThresholdSq = 0.0009f;
    constexpr float kAngularThresholdSq = 0.0004f;

    for (RigidBody& b : bodies) {
        // A camera is never asleep - it is not simulated at all, so it has no
        // quiet to measure.
        if (b.asleep || b.material.isStatic || b.isCamera()) continue;
        const bool quiet = b.touching &&
                           b.velocity.lengthSq() < kLinearThresholdSq &&
                           b.angularVelocity.lengthSq() < kAngularThresholdSq;
        if (!quiet) {
            b.sleepTime = 0.0f;
            continue;
        }
        b.sleepTime += dt;
        if (b.sleepTime >= 0.5f) {
            b.asleep = true;
            b.velocity = Vec3{};
            b.angularVelocity = Vec3{};
        }
    }
}

} // namespace s3d