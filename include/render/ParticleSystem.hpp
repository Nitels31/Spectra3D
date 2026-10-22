#pragma once
#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/ShaderProgram.hpp"

namespace s3d {

struct Camera;
class PhysicsWorld;
struct FogSettings;

// HOW A PARTICLE IS PUT ON THE SCREEN
//
// Two ways, and they are not the same thing:
//
//   Alpha     - the particle is drawn OVER what is behind it. Smoke is alpha:
//               it hides the thing behind it, and the more of it there is, the
//               less of the thing behind it there is.
//
//   Additive  - the particle is ADDED to what is behind it. Fire is additive:
//               it does not hide the ground, it makes it brighter, and a wall
//               of fire in front of a wall is a bright wall and not a red one.
//
// The difference is one blend function, and it is the difference between a
// thing that is there and a thing that is glowing.
enum class ParticleBlend {
    Alpha,
    Additive,
};

// THE EFFECTS THE ENGINE KNOWS BY NAME
//
// A script says `engine::emitParticles(x, y, z, 40, "fire")` and the engine
// knows what fire is. The names are the same in every language file and in
// every scene file, so an effect can be asked for by a person and by a script
// and mean the same thing.
//
// Five of them, and they are five pictures rather than five numbers: the
// numbers that make a fire are the numbers that make a fire, and a person
// who wants fire should not have to know them.
enum class ParticlePreset { Fire, Smoke, Spark, Dust, Explosion, Count };

// How many presets there are. The panel asks for the name and the index, so
// the two cannot get out of step.
inline constexpr int kParticlePresetCount = (int)ParticlePreset::Count;

// The name of a preset, lower case: "fire", "smoke". The name a scene file
// and a script both use.
const char* particlePresetName(ParticlePreset preset);

// The preset a name stands for. False when the name is not one of them, and
// the caller decides what to do about it.
bool particlePresetFromName(const std::string& name, ParticlePreset& out);

// THE RECIPE OF ONE KIND OF PARTICLE
//
// This is what an effect is made of: how many particles come out, how fast
// they go, how long they live, how big they are and what colour they are.
// Two effects with the same recipe are the same effect.
//
// The numbers are the ones a person would use to describe it: a speed in
// metres a second, a size in metres, a life in seconds. Nothing here is a
// fraction of anything.
struct ParticleSettings {
    // How many particles one burst makes.
    int count = 24;

    // How long a particle lives, and how much of that is random. A particle
    // that always lives exactly the same time is a clock; a little random in
    // it is what makes a fire look like fire.
    float lifetime = 1.4f;
    float lifetimeJitter = 0.35f;

    // How fast it leaves, and how much of that is random.
    float speed = 2.0f;
    float speedJitter = 0.8f;

    // How wide the cone of directions is, in degrees. Zero is a single
    // direction - a jet - and 180 is every direction at once.
    float spread = 25.0f;

    // The gravity of the particle, in its own right. Smoke rises because
    // nothing pulls it down, and a spark falls because something does: the
    // gravity of the world is not the gravity of a particle.
    Vec3 gravity{0.0f, 0.0f, 0.0f};

    // How fast the air takes the speed out of it, per second. Zero is a
    // particle that keeps its speed for ever, and that is a bullet and not a
    // puff of smoke.
    float drag = 0.5f;

    // How big it is when it is born, and how big it is when it dies. Zero at
    // the end means "the same size it was born with".
    float size = 0.25f;
    float sizeEnd = 0.0f;
    // How much of the size is random.
    float sizeJitter = 0.05f;

    // The colour when it is born and the colour when it dies. The particle
    // walks from the first to the second over its life.
    Vec3 colour{1.0f, 0.55f, 0.15f};
    Vec3 colourEnd{0.5f, 0.05f, 0.0f};

    // How see-through it is when it is born and when it dies. It fades out as
    // it dies by default, because an effect that ends in a row of hard edges
    // is an effect that pops.
    float alpha = 1.0f;
    float alphaEnd = 0.0f;

    // Whether it is drawn over what is behind it or added to it.
    ParticleBlend blend = ParticleBlend::Additive;
};

// The recipe of one of the named presets.
ParticleSettings particlePresetSettings(ParticlePreset preset);

// A PLACE PARTICLES COME FROM
//
// An emitter is a point in the world, a direction, and a recipe. It is scene
// data: it is in the scene file, it is edited in the editor, and it is what
// the particle system runs.
//
// The position is in the world, and the direction is the way the particles
// go - unless the emitter rides on a body, and then both are in the body own
// frame: the position is where it is on the body, and the direction is the
// way out of it. An exhaust is a nozzle on the back of a thing, and a nozzle
// that does not turn with the thing is a nozzle that sprays sideways.
struct Emitter {
    // Where the particles come from, and which way they go.
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 direction{0.0f, 1.0f, 0.0f};

    // What they are made of.
    ParticleSettings settings;

    // The body the emitter rides on, or -1 for one that stands still.
    int body = -1;

    // Whether it keeps making bursts, and how many seconds are between them.
    // An emitter that does not is a single burst: it fires once, when it is
    // switched on, and then it is done.
    bool continuous = false;
    float interval = 1.0f;

    // Whether it is switched on at all.
    bool enabled = true;

    // How long until the next burst, and whether the first one has been
    // fired. Runtime state, not scene data: a scene that is opened starts
    // its emitters again from the beginning.
    float timer = 0.0f;
    bool fired = false;
    // How many bursts this emitter has made. It is in the seed of the
    // random numbers of each of them, so the same burst is the same burst on
    // every machine.
    int bursts = 0;
};

// ONE PARTICLE
//
// The smallest thing the engine draws that is not a model. It is a point in
// the world, a colour, a size and how long it has left.
struct Particle {
    Vec3 position;
    Vec3 velocity;
    // The gravity and the drag of the one it came from, copied in: a particle
    // keeps the recipe it was born with, and an emitter whose recipe is
    // changed does not change the particles that are already in the air.
    Vec3 gravity;
    float drag = 0.0f;

    Vec3 colourStart;
    Vec3 colourEnd;
    float size = 0.2f;
    float sizeEnd = 0.0f;
    float alpha = 1.0f;
    float alphaEnd = 0.0f;

    float age = 0.0f;
    float lifetime = 1.0f;
    ParticleBlend blend = ParticleBlend::Additive;
    bool alive = false;
};

// THE PARTICLES OF THE WORLD
//
// The pool, the emitters, the step and the drawing.
//
// It is an instance and not a global: a game that has no effects has none of
// these, and a project that has three emitters has three.
//
// THE POOL IS FIXED AND IT IS A RING. The particles are kept in one array of
// a known size, and a new one takes the place of the oldest. A pool that
// grows is a pool that allocates in the middle of a frame, and a frame that
// allocates is a frame that stutters; a ring that is too small is an effect
// that loses its oldest particles, which is what an effect does when there
// are too many of them.
class ParticleSystem {
public:
    // How many particles there is room for. Four thousand is a lot of
    // particles and it is two hundred and fifty of them a second for a
    // minute and a half.
    static constexpr int kMaxParticles = 4096;

    ParticleSystem();
    ~ParticleSystem();
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // The program and the buffers. A program that does not load is not a
    // reason to stop: the engine draws the scene with what it has, and the
    // log says what is missing.
    void initialize();
    void release();

    // The emitters: the places particles come from.
    std::vector<Emitter>& emitters() { return emitters_; }
    const std::vector<Emitter>& emitters() const { return emitters_; }
    // The emitter is added and its index is returned, or -1 when it could not
    // be added.
    int addEmitter(const Emitter& emitter);
    void removeEmitter(int index);
    void clearEmitters();

    // The body at `index` is gone from the world.
    //
    // The emitters that rode on it are free now - there is nothing to ride
    // on - and the ones that rode on a body after it are one index earlier,
    // because the bodies after the one that is gone have moved down. An
    // emitter that points at the wrong body is an exhaust on the wrong
    // thing, and nobody would ever find out why.
    void removeBody(int index);
    // Every body is gone: no emitter rides on anything any more.
    void removeAllBodies();

    // A burst of particles, right now. `count` of -1 means the number the
    // recipe says. The number of particles that were made is returned.
    int emit(const ParticleSettings& settings, const Vec3& position,
             const Vec3& direction, int count = -1);
    int emit(ParticlePreset preset, const Vec3& position, const Vec3& direction,
             int count = -1);

    // The step: the emitters make what they make, and the particles in the
    // air move. `world` may be null, and then the emitters that ride on a
    // body stand where they are.
    void update(float dt, const PhysicsWorld* world = nullptr);

    // How many particles are in the air.
    int liveCount() const { return liveCount_; }
    // The pool itself: every particle, alive or not. A test asks it what a
    // particle is where, and nothing else needs to.
    const std::vector<Particle>& pool() const { return particles_; }
    // True while there is anything to draw. The window draws frames while
    // this is true: a fire that nobody touches still has to burn.
    bool hasLiveParticles() const { return liveCount_ > 0; }
    // Takes every particle out of the air. The emitters stay.
    void clearParticles();

    // The seed of the random numbers. The same seed is the same effect on
    // every machine, the way the same seed is the same ground.
    void setSeed(unsigned seed) { seed_ = seed ? seed : 1u; }
    unsigned seed() const { return seed_; }

    // The drawing. The camera is what the particles face, and the two
    // matrices are the camera's own.
    //
    // The fog is the same air as everything else in the picture, and a
    // particle in a mist is a dimmer particle. It may be null, and then the
    // particles are drawn as they are.
    //
    // The sun is the light the fog carries: the air in front of a particle is
    // brighter where the sun is behind it, and a puff of smoke on the side of
    // the sun is a puff of smoke with the sun in it.
    void draw(const Camera& camera, const Mat4& view, const Mat4& projection,
              const FogSettings* fog = nullptr, const Vec3& sunDirection = Vec3{0.0f, 1.0f, 0.0f},
              const Vec3& sunColour = Vec3{1.0f, 1.0f, 1.0f});

private:
    // Makes one burst of particles out of an emitter. `salt` is what keeps
    // the bursts of two emitters apart in the random numbers.
    void burst(const Emitter& emitter, const Vec3& position, const Vec3& direction,
               unsigned salt);
    // Puts one particle into the pool, taking the place of the oldest.
    Particle& take();

    // The pool and the ring in it.
    std::vector<Particle> particles_;
    int nextParticle_ = 0;
    int liveCount_ = 0;

    // The emitters of the world.
    std::vector<Emitter> emitters_;

    // The program and the buffers.
    ShaderProgram program_;
    unsigned int quadBuffer_ = 0;
    unsigned int alphaBuffer_ = 0;
    unsigned int additiveBuffer_ = 0;
    // The instance data of the two passes, filled afresh every frame.
    std::vector<float> alphaData_;
    std::vector<float> additiveData_;

    unsigned seed_ = 1u;
};

} // namespace s3d
