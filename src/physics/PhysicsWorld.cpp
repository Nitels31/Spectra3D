#include "physics/PhysicsWorld.hpp"

#include <algorithm>
#include <cstddef>

namespace s3d {
namespace {

// Arena wall normals, indexed the same way as the warm-start cache slots.
const Vec3 kWallNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

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

void PhysicsWorld::clear() { bodies.clear(); }

void PhysicsWorld::clearWarmStartCache() {
    for (RigidBody& b : bodies)
        for (int wall = 0; wall < 6; ++wall)
            for (int slot = 0; slot < 8; ++slot) {
                b.warmNormal[wall][slot] = 0.0f;
                b.warmTangent[wall][slot] = 0.0f;
                b.warmStamp[wall][slot] = 0;
            }
}

void PhysicsWorld::wakeAll() {
    for (RigidBody& b : bodies) {
        b.wake();
        for (int w = 0; w < 6; ++w)
            for (int k = 0; k < 8; ++k) {
                b.warmNormal[w][k] = 0.0f;
                b.warmTangent[w][k] = 0.0f;
                b.warmStamp[w][k] = 0;
            }
    }
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
    previousStepStamp_ = stepStamp_;
    if (++stepStamp_ == 0) { // 0 marks "never written" - clear on wrap
        stepStamp_ = 1;
        previousStepStamp_ = 0;
        clearWarmStartCache();
    }

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
        if (b.asleep || b.material.isStatic) continue;
        b.position += b.velocity * dt;
        b.orientation = integrate(b.orientation, b.angularVelocity, dt);

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
}

void PhysicsWorld::integrateForces(float dt) {
    for (RigidBody& b : bodies) {
        b.invInertiaCached = b.invInertiaWorld();
        refreshDerived(b);
        b.touching = false;
        if (b.asleep) continue;
        if (!b.material.isStatic) {
            b.force += gravity * b.mass;
            b.velocity += b.force * b.invMass * dt;
            b.angularVelocity += b.invInertiaCached * (b.torque * dt);
        }
        b.force = Vec3{};
        b.torque = Vec3{};
    }
}

void PhysicsWorld::generateContacts() {
    collectWallContacts();
    collectPairContacts();
}

void PhysicsWorld::collectWallContacts() {
    float plane[6] = {arenaMin.x, -arenaMax.x, arenaMin.y,
                      -arenaMax.y, arenaMin.z, -arenaMax.z};

    for (uint32_t bi = 0; bi < bodies.size(); ++bi) {
        RigidBody& b = bodies[bi];
        if (b.asleep || b.material.isStatic) continue;

        if (b.shape == Shape::Sphere) {
            const float r = b.halfExtents.x;
            for (int w = 0; w < 6; ++w) {
                // Every wall participates, including the one gravity points at:
                // bodies must be able to rest on the arena floor.
                const float gap = dot(kWallNormals[w], b.position) - plane[w] - r;
                if (gap > kContactMargin) continue;
                b.touching = true;
                contacts_.push_back({bi, Contact::kNoBody, -kWallNormals[w] * r, Vec3{},
                                     kWallNormals[w], -gap, w, 0,
                                     0.0f, 0.0f, 0.0f, 0.0f});
            }
            continue;
        }

        // Oriented box: every corner within reach of a wall becomes a contact,
        // keyed by (wall, corner) so warm-start impulses survive the step.
        const float cornerMin[6] = {b.aabbMin.x - kContactMargin, -b.aabbMax.x - kContactMargin,
                                    b.aabbMin.y - kContactMargin, -b.aabbMax.y - kContactMargin,
                                    b.aabbMin.z - kContactMargin, -b.aabbMax.z - kContactMargin};
        for (int w = 0; w < 6; ++w) {
            if (cornerMin[w] >= plane[w]) continue;
            for (int k = 0; k < 8; ++k) {
                const float gap = dot(kWallNormals[w], b.corners[k]) - plane[w];
                if (gap > kContactMargin) continue;
                b.touching = true;
                contacts_.push_back({bi, Contact::kNoBody, b.corners[k] - b.position,
                                     Vec3{}, kWallNormals[w], -gap, w, k,
                                     0.0f, 0.0f, 0.0f, 0.0f});
            }
        }
    }
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
        if (b.material.isStatic) continue;
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
                        // Two bodies that share a cell are visited by both
                        // queries, so only the lower index reports the pair.
                        // This halves the number of keys to sort while
                        // producing the exact same sorted, unique pair list.
                        if (it->second <= bi) continue;
                        pairKeys_.push_back(pairKey(bi, it->second));
                    }
                }
    }

    std::sort(pairKeys_.begin(), pairKeys_.end());
    pairKeys_.erase(std::unique(pairKeys_.begin(), pairKeys_.end()), pairKeys_.end());

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

        if (aSphere && bSphere) {
            const float rA = A.halfExtents.x, rB = B.halfExtents.x;
            const float dist = d.length();
            if (dist < 1e-6f || dist >= rA + rB) continue;
            const Vec3 n = d / dist;
            A.touching = B.touching = true;
            contacts_.push_back({iA, iB, n * rA, n * (-rB), n, rA + rB - dist,
                                 Contact::kNoWall, 0, 0.0f, 0.0f, 0.0f, 0.0f});
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
                                 Contact::kNoWall, 0, 0.0f, 0.0f, 0.0f, 0.0f});
            continue;
        }

// Box vs box: sample each corner of A against B, then B against A.
        //
        // The quaternion conjugates and the half extents are loop invariant -
        // hoisting them out saves 14 redundant conversions per pair.
        const Quat bInverse = B.orientation.conjugate();
        const Quat aInverse = A.orientation.conjugate();
        const Vec3& halfB = B.halfExtents;
        const Vec3& halfA = A.halfExtents;

        for (int k = 0; k < 8; ++k) {
            const Vec3 local = rotate(bInverse, A.corners[k] - B.position);
            const float dx = halfB.x - std::fabs(local.x);
            const float dy = halfB.y - std::fabs(local.y);
            const float dz = halfB.z - std::fabs(local.z);
            if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f) continue;
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
            Vec3 localNormal{};
            if (axis == 0) localNormal.x = sign;
            else if (axis == 1) localNormal.y = sign;
            else localNormal.z = sign;
            A.touching = B.touching = true;
            contacts_.push_back({iA, iB, A.corners[k] - A.position,
                                 A.corners[k] - B.position,
                                 rotate(B.orientation, localNormal), shallowest,
                                 Contact::kNoWall, k, 0.0f, 0.0f, 0.0f, 0.0f});
        }
        for (int k = 0; k < 8; ++k) {
            const Vec3 local = rotate(aInverse, B.corners[k] - A.position);
            const float dx = halfA.x - std::fabs(local.x);
            const float dy = halfA.y - std::fabs(local.y);
            const float dz = halfA.z - std::fabs(local.z);
            if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f) continue;
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
            Vec3 localNormal{};
            if (axis == 0) localNormal.x = sign;
            else if (axis == 1) localNormal.y = sign;
            else localNormal.z = sign;
            A.touching = B.touching = true;
            contacts_.push_back({iA, iB, B.corners[k] - A.position,
                                 B.corners[k] - B.position,
                                 -rotate(A.orientation, localNormal), shallowest,
                                 Contact::kNoWall, k, 0.0f, 0.0f, 0.0f, 0.0f});
        }
    }
}

void PhysicsWorld::applyWarmStart() {
    for (Contact& c : contacts_) {
        // A contact that still has a gap must not receive an impulse: doing so
        // would launch the body as soon as it re-enters contact.
        if (c.penetration < 0.0f) continue;

        float jn, jt;
        if (c.bodyB != Contact::kNoBody) {
            jn = c.warmNormal;
            jt = c.warmTangent;
        } else {
            RigidBody& b = bodies[c.bodyA];
            if (b.material.isStatic) continue;
            // Only an impulse written by the previous step is still valid. This
            // replaces sweeping the whole 6x8 cache of every body each step, and
            // it also stops a body that flew away from being kicked by an
            // impulse left over from its previous landing.
            if (b.warmStamp[c.wall][c.slot] != previousStepStamp_) continue;
            jn = b.warmNormal[c.wall][c.slot];
            jt = b.warmTangent[c.wall][c.slot];
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
        if (c.bodyB != Contact::kNoBody) {
            c.warmNormal = std::clamp(c.accumulatedNormal, 0.0f, 1e6f);
            const float maxTangent = 0.6f * std::fabs(c.accumulatedNormal);
            c.warmTangent = std::clamp(c.accumulatedTangent, -maxTangent, maxTangent);
        } else {
            RigidBody& b = bodies[c.bodyA];
            b.warmNormal[c.wall][c.slot] = std::clamp(c.accumulatedNormal, 0.0f, 1e6f);
            const float maxTangent = 0.6f * std::fabs(c.accumulatedNormal);
            b.warmTangent[c.wall][c.slot] =
                std::clamp(c.accumulatedTangent, -maxTangent, maxTangent);
            b.warmStamp[c.wall][c.slot] = stepStamp_;
        }
    }

    // No cache sweep is needed: applyWarmStart only accepts an impulse whose
    // stamp is from the immediately preceding step, so anything older is inert
    // and costs nothing to skip.
}

void PhysicsWorld::resolvePenetrations() {
    const std::size_t n = bodies.size();
    if (n == 0 || contacts_.empty()) return;

    // Arena walls: push each body out along its deepest contact only.
    // Scratch buffers are reused between steps - this runs 60 times a second and
    // two heap allocations per step is pure overhead.
    if (deepestPenetration_.size() < n) {
        deepestPenetration_.resize(n);
        pushDirection_.resize(n);
    }
    std::fill(deepestPenetration_.begin(), deepestPenetration_.begin() + n, 0.0f);

    for (const Contact& c : contacts_) {
        if (c.bodyB != Contact::kNoBody) continue;
        const uint32_t idx = c.bodyA;
        if (idx >= n || bodies[idx].asleep || bodies[idx].material.isStatic) continue;
        if (c.penetration > deepestPenetration_[idx]) {
            deepestPenetration_[idx] = c.penetration;
            pushDirection_[idx] = c.normal;
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        if (deepestPenetration_[i] > kPenetrationSlop)
            bodies[i].position += pushDirection_[i] *
                                  ((deepestPenetration_[i] - kPenetrationSlop) * kWallCorrection);

    // Body pairs: split the correction by inverse mass.
    for (const Contact& c : contacts_) {
        if (c.bodyB == Contact::kNoBody) continue;
        RigidBody& A = bodies[c.bodyA];
        RigidBody& B = bodies[c.bodyB];
        const float invA = (A.asleep || A.material.isStatic) ? 0.0f : A.invMass;
        const float invB = (B.asleep || B.material.isStatic) ? 0.0f : B.invMass;
        const float sum = invA + invB;
        if (sum <= 0.0f) continue;
        const float correction = (c.penetration - kPenetrationSlop) * kPairCorrection;
        if (correction <= 0.0f) continue;
        if (invA > 0.0f) A.position += c.normal * (correction * invA / sum);
        if (invB > 0.0f) B.position -= c.normal * (correction * invB / sum);
    }
}

void PhysicsWorld::updateSleep(float dt) {
    constexpr float kLinearThresholdSq = 0.0009f;
    constexpr float kAngularThresholdSq = 0.0004f;

    for (RigidBody& b : bodies) {
        if (b.asleep || b.material.isStatic) continue;
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