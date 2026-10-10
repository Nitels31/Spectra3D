#pragma once
#include <cstdint>
#include <vector>

#include "physics/RigidBody.hpp"

namespace s3d {

enum class GravityAxis { NegX = 0, PosX = 1, NegY = 2, PosY = 3, NegZ = 4, PosZ = 5 };

// Owns every body and advances the simulation. Contact generation is split into
// arena walls and body-body pairs; the latter uses a sort-based spatial hash so
// cost stays close to linear in body count.
class PhysicsWorld {
public:
    Vec3 gravity{0.0f, -9.80665f, 0.0f};
    float gravityMagnitude = 9.80665f;
    GravityAxis gravityAxis = GravityAxis::NegY;
    Vec3 arenaMin{-60.0f, -9.0f, -60.0f};
    Vec3 arenaMax{60.0f, 150.0f, 60.0f};

    std::vector<RigidBody> bodies;

    void setGravity(GravityAxis axis);
    void clear();
    void wakeAll();

    void step(float dt);

    Vec3 upDirection() const;
    int sleepingCount() const;

    RigidBody* find(int index);
    const RigidBody* find(int index) const;
    void remove(int index);

private:
    // One resolved contact for the current step. `bodyB` == kNoBody for arena
    // walls, in which case `wall`/`slot` address the warm-start cache.
struct Contact {
        static constexpr uint32_t kNoBody = UINT32_MAX;
        static constexpr int kNoWall = -1;

        uint32_t bodyA;
        uint32_t bodyB;
        Vec3 armA;
        Vec3 armB;
        Vec3 normal;
        float penetration;
        int wall;
        int slot;
        float accumulatedNormal;
        float accumulatedTangent;
        float warmNormal;
        float warmTangent;
        // Index into the pair warm-start table for a body-body contact, -1 for a
        // wall contact (which keeps its cache in the RigidBody instead). Set only
        // for pairs; the default leaves every wall contact's -1.
        int pairIndex = -1;
    };

    void integrateForces(float dt);
    void generateContacts();
    void collectWallContacts();
    void collectPairContacts();
    void applyWarmStart();
    bool solveIteration();
    void storeWarmStart();
    void clearWarmStartCache();
    void resolvePenetrations();
    void updateSleep(float dt);
    void refreshDerived(RigidBody& body) const;

std::vector<Contact> contacts_;
    std::vector<std::pair<uint64_t, uint32_t>> cellEntries_;
    std::vector<uint64_t> pairKeys_;

    // Warm-start impulses for body-body pairs, keyed by the canonical pair key and
    // then by contact slot within the pair - the same idea the wall cache uses
    // (wall + corner slot), so a box-box pair's 16 contact points each keep their
    // own impulse instead of sharing one number.
    //
    // The cache cannot live on the Contact: contacts_ is rebuilt from scratch
    // every step, so anything written to a contact is gone before it is read
    // again. That is why body-body contacts were never warm-started - the solver
    // never converged, its early-out never fired, and stacked bodies never slept.
    //
    // A pair produces at most 16 points (box-box samples 8 corners of A against B
    // and 8 of B against A), so the table is pairCount * kPairSlots wide.
    static constexpr int kPairSlots = 16;
    struct PairWarm {
        float normal = 0.0f;
        float tangent = 0.0f;
    };
    // Both key arrays are sorted by the same canonical pair key and are walked in
    // the same order, so seeding is a linear merge, not a lookup. The two value
    // arrays are swapped at the end of each step, so nothing allocates after the
    // first few frames.
    std::vector<uint64_t> pairWarmKeys_;
    std::vector<PairWarm> pairWarmValues_;
    std::vector<uint64_t> pairWarmNextKeys_;
    std::vector<PairWarm> pairWarmNextValues_;

    // Writes one fresh (zeroed) seed row into the next-step table and returns the
    // pair index the caller should stamp on its contacts. Called once per pair,
    // from the same sorted sequence that produced the key, so `cursor` advances
    // monotonically across calls.
    int seedPairWarm(uint64_t key, size_t& cursor);
    void clearPairWarm();

    // Steps since the world was created; used to invalidate warm-start caches.
    uint16_t stepStamp_ = 0;

    uint16_t previousStepStamp_ = 0;

    // Reused by resolvePenetrations so the step allocates nothing.
    std::vector<float> deepestPenetration_;
    std::vector<Vec3> pushDirection_;
    // Same, for body pairs: the deepest contact of each pair, so one pair is
    // pushed out once instead of once per contact point.
    std::vector<float> pairDeepest_;
    std::vector<Vec3> pairPush_;
    float currentStepDt_ = 1.0f / 60.0f;
};

const char* gravityName(GravityAxis axis);

} // namespace s3d