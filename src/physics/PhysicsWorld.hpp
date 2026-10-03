#pragma once
#include <cstdint>
#include <vector>

#include "RigidBody.hpp"

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

    // Steps since the world was created; used to invalidate warm-start caches.
    uint16_t stepStamp_ = 0;

    uint16_t previousStepStamp_ = 0;

    // Reused by resolvePenetrations so the step allocates nothing.
    std::vector<float> deepestPenetration_;
    std::vector<Vec3> pushDirection_;
    float currentStepDt_ = 1.0f / 60.0f;
};

const char* gravityName(GravityAxis axis);

} // namespace s3d