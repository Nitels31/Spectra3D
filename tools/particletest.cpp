// Headless check of the particle system: the pool, the step, the emitters and
// the names of the effects.
//
// The simulation touches no GL - a particle is a point, a colour and a size -
// so the whole of it can be checked with no window and no context. The
// drawing is not checked here: what is on the screen is a picture, and a
// picture is checked by looking at it.
//
// What is checked here is the contract a caller relies on: a burst makes the
// number of particles the recipe says, the pool is a ring and not a heap, a
// particle dies when its life is up, the same seed makes the same effect, and
// an emitter that is not continuous fires once and is done.

#include "render/ParticleSystem.hpp"

#include "core/Log.hpp"
#include "physics/PhysicsWorld.hpp"

#include <cstdio>
#include <string>

using namespace s3d;

namespace {

int g_checks = 0;
int g_failures = 0;

// One line per assertion so a failure names the behaviour that broke.
void check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        std::printf("  ok   %s\n", what);
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", what);
}

void heading(const char* what) { std::printf("\n== %s ==\n", what); }

} // namespace

int main() {
    Log::clear();

    heading("the names of the effects");
    for (int i = 0; i < kParticlePresetCount; ++i) {
        const ParticlePreset preset = (ParticlePreset)i;
        const char* name = particlePresetName(preset);
        ParticlePreset found = ParticlePreset::Count;
        check(name && *name && particlePresetFromName(name, found) && found == preset,
              "the name of an effect finds the effect");
    }
    ParticlePreset none = ParticlePreset::Fire;
    check(!particlePresetFromName("no-such-effect", none),
          "a name that is not an effect is not one");

    heading("a burst");
    {
        ParticleSystem system;
        ParticleSettings settings = particlePresetSettings(ParticlePreset::Fire);
        settings.count = 30;
        settings.lifetime = 1.0f;
        settings.lifetimeJitter = 0.0f;
        settings.speed = 1.0f;
        settings.speedJitter = 0.0f;
        settings.spread = 0.0f;

        const int made = system.emit(settings, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
        check(made == 30, "a burst makes the number of particles the recipe says");
        check(system.liveCount() == 30, "the particles are in the air");
    }

    heading("the step");
    {
        ParticleSystem system;
        ParticleSettings settings;
        settings.count = 10;
        settings.lifetime = 0.5f;
        settings.lifetimeJitter = 0.0f;
        settings.speed = 2.0f;
        settings.speedJitter = 0.0f;
        settings.spread = 0.0f;
        settings.gravity = Vec3{0.0f, -10.0f, 0.0f};
        settings.drag = 0.0f;
        system.emit(settings, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});

        system.update(0.1f, nullptr);
        check(system.liveCount() == 10, "the particles are still in the air after a step");

        // Six steps of a tenth of a second: more than half a second, so they
        // are all past their life.
        for (int i = 0; i < 6; ++i) system.update(0.1f, nullptr);
        check(system.liveCount() == 0, "a particle dies when its life is up");
    }

    heading("the pool is a ring");
    {
        ParticleSystem system;
        ParticleSettings settings;
        settings.count = 10;
        settings.lifetime = 100.0f;
        settings.lifetimeJitter = 0.0f;
        system.emit(settings, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});

        // More particles than the pool has room for: the oldest ones are taken
        // back, and the number in the air is the size of the pool and not
        // more.
        const int bursts = ParticleSystem::kMaxParticles / 5 + 4;
        for (int i = 0; i < bursts; ++i)
            system.emit(settings, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
        check(system.liveCount() == ParticleSystem::kMaxParticles,
              "the pool never holds more than it has room for");

        // And the pool is emptied, not forgotten.
        system.clearParticles();
        check(system.liveCount() == 0, "the particles can be taken out of the air");
    }

    heading("the same seed, the same effect");
    {
        ParticleSystem first;
        ParticleSystem second;
        first.setSeed(1234);
        second.setSeed(1234);

        ParticleSettings settings = particlePresetSettings(ParticlePreset::Explosion);
        settings.lifetimeJitter = 0.5f;
        settings.speedJitter = 2.0f;
        first.emit(settings, Vec3{1.0f, 2.0f, 3.0f}, Vec3{0.0f, 1.0f, 0.0f});
        second.emit(settings, Vec3{1.0f, 2.0f, 3.0f}, Vec3{0.0f, 1.0f, 0.0f});
        first.update(0.016f, nullptr);
        second.update(0.016f, nullptr);

        // The same seed, the same effect: the particles are in the same
        // places and they are the same size. The two systems are separate,
        // and the check is that nothing in them is random twice.
        bool same = first.liveCount() == second.liveCount();
        for (size_t i = 0; same && i < first.pool().size() && i < second.pool().size(); ++i) {
            const Particle& a = first.pool()[i];
            const Particle& b = second.pool()[i];
            same = a.position.x == b.position.x && a.position.y == b.position.y &&
                   a.position.z == b.position.z && a.size == b.size && a.lifetime == b.lifetime;
        }
        check(same, "the same seed makes the same effect");
    }

    heading("the emitters");
    {
        ParticleSystem system;
        ParticleSettings settings;
        settings.count = 5;
        settings.lifetime = 100.0f;
        settings.lifetimeJitter = 0.0f;

        Emitter once;
        once.settings = settings;
        once.position = Vec3{0.0f, 0.0f, 0.0f};
        system.addEmitter(once);

        system.update(0.016f, nullptr);
        check(system.liveCount() == 5, "an emitter makes its first burst");
        system.update(0.016f, nullptr);
        system.update(0.016f, nullptr);
        check(system.liveCount() == 5, "an emitter that is not continuous fires once and is done");

        // And one that keeps going.
        ParticleSystem steady;
        Emitter always;
        always.settings = settings;
        always.continuous = true;
        always.interval = 0.1f;
        steady.addEmitter(always);
        steady.update(0.016f, nullptr);
        const int afterFirst = steady.liveCount();
        // A fifth of a second is five intervals, so the emitter has made
        // more than one burst since.
        steady.update(0.2f, nullptr);
        check(steady.liveCount() > afterFirst, "an emitter that is continuous keeps making");
    }

    heading("an emitter on a body");
    {
        ParticleSystem system;
        PhysicsWorld world;
        RigidBody body;
        body.position = Vec3{5.0f, 0.0f, 0.0f};
        world.bodies.push_back(body);

        ParticleSettings settings;
        settings.count = 3;
        settings.lifetime = 100.0f;
        settings.lifetimeJitter = 0.0f;

        Emitter emitter;
        emitter.settings = settings;
        emitter.body = 0;
        emitter.position = Vec3{0.0f, 1.0f, 0.0f};
        system.addEmitter(emitter);

        system.update(0.016f, &world);
        check(system.liveCount() == 3, "an emitter on a body makes its particles");

        // The body moves, and the emitter goes with it: the emitter is in the
        // body own frame, so the particles come out of the body and not out
        // of the place the body was.
        world.bodies[0].position = Vec3{9.0f, 0.0f, 0.0f};
        system.emitters()[0].fired = false;
        system.update(0.016f, &world);
        check(system.liveCount() == 6, "an emitter on a body makes more when it is asked to");

        // And the particles it made are at the body and not at the origin.
        bool atBody = false;
        for (const Particle& particle : system.pool()) {
            if (!particle.alive) continue;
            if (particle.position.x > 8.0f) atBody = true;
        }
        check(atBody, "the particles of an emitter on a body come out of the body");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
