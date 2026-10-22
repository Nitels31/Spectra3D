#include "render/ParticleSystem.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "platform/GlFunctions.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Fog.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// How many floats one particle takes in the buffer: the centre (3), the size
// and the alpha (2), and the colour (3).
constexpr int kInstanceFloats = 8;

// The four corners of the quad, in the order the strip wants them. The same
// four numbers for every particle, written into their own buffer once.
constexpr float kQuadCorners[8] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};

// How many bursts one emitter may make in one frame. A frame that is a
// second long would otherwise make sixty bursts of a fire that is meant to
// make one every two seconds, and the picture would be a wall.
constexpr int kMaxBurstsPerFrame = 8;

// A RANDOM NUMBER GENERATOR OF THE ENGINE'S OWN
//
// The same seed is the same effect on every machine, the way the same seed is
// the same ground: an effect that looks different on two machines is an
// effect that cannot be tested, and an effect that cannot be tested is an
// effect nobody trusts.
//
// xorshift32: three shifts and three xors, and the same number on every
// machine - which a sine is not.
class Random {
public:
    explicit Random(unsigned seed) : state_(seed ? seed : 1u) {}

    unsigned next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    // 0..1
    float unit() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }

    // Between two numbers, in either order.
    float range(float low, float high) { return low + (high - low) * unit(); }

    // A direction in the cone around `axis`.
    //
    // The angle around the axis is even, and the distance from it is the
    // square root of a random number - so the particles are spread over the
    // whole cone instead of gathering in the middle of it, which is what a
    // plain random number in the radius would do.
    Vec3 cone(const Vec3& axis, float spreadDegrees);

private:
    unsigned state_;
};

Vec3 Random::cone(const Vec3& axis, float spreadDegrees) {
    const Vec3 up = axis.lengthSq() > 1e-8f ? axis.normalized() : Vec3{0.0f, 1.0f, 0.0f};

    // Two axes across the direction, so that the cone can be measured in
    // them. The world's up is not across every direction, so another one is
    // used when the two are too close to each other.
    Vec3 side = cross(up, std::fabs(up.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f}
                                                  : Vec3{1.0f, 0.0f, 0.0f});
    if (side.lengthSq() < 1e-8f) side = cross(up, Vec3{0.0f, 0.0f, 1.0f});
    side = side.normalized();
    const Vec3 other = cross(up, side);

    const float angle = unit() * 2.0f * kPi;
    const float radius =
        std::sqrt(unit()) * std::tan(clampf(spreadDegrees, 0.0f, 180.0f) * kPi / 180.0f);

    return (up + side * (std::cos(angle) * radius) + other * (std::sin(angle) * radius))
        .normalized();
}

// ONE PASS OF THE PARTICLES
//
// The two kinds of particle are drawn in two passes, because they are
// blended differently: one is drawn over what is behind it and one is added
// to it, and a blend function is state and not a picture.
//
// The four corners of the quad are in their own buffer, and the data of the
// particles is in another one: the corners are the same for every particle
// and the data is not, and the divisor is what tells the driver which is
// which.
void drawPass(unsigned int buffer, const std::vector<float>& data, GLenum source,
              GLenum destination) {
    if (data.empty()) return;

    const size_t stride = sizeof(float) * kInstanceFloats;

    gl::BindBuffer(GL_ARRAY_BUFFER, buffer);
    gl::BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(data.size() * sizeof(float)),
                       data.data());

    // The centre of the particle, the size and the alpha, the colour. The
    // same for all four corners of the quad, so the divisor is one.
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (const void*)0);
    gl::VertexAttribDivisor(0, 1);

    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                            (const void*)(3 * sizeof(float)));
    gl::VertexAttribDivisor(1, 1);

    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride,
                            (const void*)(5 * sizeof(float)));
    gl::VertexAttribDivisor(2, 1);

    glBlendFunc(source, destination);
    gl::DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)(data.size() / kInstanceFloats));
}

} // namespace

const char* particlePresetName(ParticlePreset preset) {
    switch (preset) {
    case ParticlePreset::Fire: return "fire";
    case ParticlePreset::Smoke: return "smoke";
    case ParticlePreset::Spark: return "spark";
    case ParticlePreset::Dust: return "dust";
    case ParticlePreset::Explosion: return "explosion";
    case ParticlePreset::Count: break;
    }
    return "fire";
}

bool particlePresetFromName(const std::string& name, ParticlePreset& out) {
    for (int i = 0; i < kParticlePresetCount; ++i) {
        const ParticlePreset preset = (ParticlePreset)i;
        if (name == particlePresetName(preset)) {
            out = preset;
            return true;
        }
    }
    return false;
}

ParticleSettings particlePresetSettings(ParticlePreset preset) {
    ParticleSettings settings;
    switch (preset) {
    case ParticlePreset::Fire:
        // A fire: quick out of the nozzle, held back by the air, white-hot
        // at the start and red at the end. It is added to what is behind it,
        // because a fire does not hide the wall it is in front of - it makes
        // the wall brighter.
        settings.count = 28;
        settings.lifetime = 0.9f;
        settings.lifetimeJitter = 0.35f;
        settings.speed = 1.8f;
        settings.speedJitter = 0.9f;
        settings.spread = 18.0f;
        settings.drag = 1.6f;
        settings.size = 0.22f;
        settings.sizeEnd = 0.05f;
        settings.sizeJitter = 0.06f;
        settings.colour = Vec3{1.0f, 0.85f, 0.45f};
        settings.colourEnd = Vec3{0.85f, 0.15f, 0.02f};
        settings.alpha = 1.0f;
        settings.alphaEnd = 0.0f;
        settings.blend = ParticleBlend::Additive;
        break;

    case ParticlePreset::Smoke:
        // Smoke: slow, wide, and it hides what is behind it. It rises,
        // because nothing pulls it down, and it gets bigger as it goes.
        settings.count = 18;
        settings.lifetime = 2.6f;
        settings.lifetimeJitter = 0.6f;
        settings.speed = 0.9f;
        settings.speedJitter = 0.4f;
        settings.spread = 12.0f;
        settings.gravity = Vec3{0.0f, 0.6f, 0.0f};
        settings.drag = 0.9f;
        settings.size = 0.45f;
        settings.sizeEnd = 1.10f;
        settings.sizeJitter = 0.10f;
        settings.colour = Vec3{0.32f, 0.30f, 0.29f};
        settings.colourEnd = Vec3{0.62f, 0.62f, 0.64f};
        settings.alpha = 0.55f;
        settings.alphaEnd = 0.0f;
        settings.blend = ParticleBlend::Alpha;
        break;

    case ParticlePreset::Spark:
        // A spark: fast, small, and it falls, because the world pulls it
        // down. It stays bright until it dies.
        settings.count = 20;
        settings.lifetime = 0.7f;
        settings.lifetimeJitter = 0.3f;
        settings.speed = 6.0f;
        settings.speedJitter = 3.0f;
        settings.spread = 25.0f;
        settings.gravity = Vec3{0.0f, -9.8f, 0.0f};
        settings.drag = 0.2f;
        settings.size = 0.06f;
        settings.sizeEnd = 0.02f;
        settings.sizeJitter = 0.01f;
        settings.colour = Vec3{1.0f, 0.92f, 0.55f};
        settings.colourEnd = Vec3{1.0f, 0.40f, 0.05f};
        settings.alpha = 1.0f;
        settings.alphaEnd = 0.0f;
        settings.blend = ParticleBlend::Additive;
        break;

    case ParticlePreset::Dust:
        // Dust: slow, wide, brown, and it settles.
        settings.count = 24;
        settings.lifetime = 2.2f;
        settings.lifetimeJitter = 0.5f;
        settings.speed = 0.7f;
        settings.speedJitter = 0.3f;
        settings.spread = 40.0f;
        settings.gravity = Vec3{0.0f, -0.6f, 0.0f};
        settings.drag = 1.2f;
        settings.size = 0.30f;
        settings.sizeEnd = 0.50f;
        settings.sizeJitter = 0.08f;
        settings.colour = Vec3{0.62f, 0.55f, 0.42f};
        settings.colourEnd = Vec3{0.70f, 0.66f, 0.55f};
        settings.alpha = 0.50f;
        settings.alphaEnd = 0.0f;
        settings.blend = ParticleBlend::Alpha;
        break;

    case ParticlePreset::Explosion:
        // An explosion: everything at once, in every direction, and it is
        // over quickly.
        settings.count = 60;
        settings.lifetime = 1.1f;
        settings.lifetimeJitter = 0.4f;
        settings.speed = 7.0f;
        settings.speedJitter = 4.0f;
        settings.spread = 180.0f;
        settings.gravity = Vec3{0.0f, -3.0f, 0.0f};
        settings.drag = 2.2f;
        settings.size = 0.50f;
        settings.sizeEnd = 0.05f;
        settings.sizeJitter = 0.15f;
        settings.colour = Vec3{1.0f, 0.95f, 0.70f};
        settings.colourEnd = Vec3{0.90f, 0.25f, 0.05f};
        settings.alpha = 1.0f;
        settings.alphaEnd = 0.0f;
        settings.blend = ParticleBlend::Additive;
        break;

    case ParticlePreset::Count:
        break;
    }
    return settings;
}

ParticleSystem::ParticleSystem() {
    // The pool, made here and not in initialize().
    //
    // The pool is the CPU's half of the particles and it needs no context: a
    // particle is a point, a colour and a size, and a system that can only be
    // filled where there is a window cannot be tested where there is none -
    // which is exactly where the simulation is tested.
    particles_.assign((size_t)kMaxParticles, Particle{});
}

ParticleSystem::~ParticleSystem() { release(); }

void ParticleSystem::initialize() {
    // The particles have a folder of their own, and the path says so: the
    // folder is where the things that belong together live, and the loader
    // wants the path and not the name.
    program_.load(shaderPath("particles/particles.vert"),
                  shaderPath("particles/particles.frag"));
    if (!program_.ready()) {
        // A program that does not load is not a reason to stop: the scene is
        // drawn with what there is, and the log says what is missing.
        Log::warn("Particles: the program did not load, the particles are not drawn");
        return;
    }
    if (!gl::GenBuffers || !gl::BufferData || !gl::DrawArraysInstanced) {
        Log::warn("Particles: the driver has no buffers, the particles are not drawn");
        return;
    }

    // The pool. It is made once and it never grows: a pool that grows is a
    // pool that allocates in the middle of a frame.
    particles_.assign((size_t)kMaxParticles, Particle{});

    // The four corners of the quad, written once.
    gl::GenBuffers(1, &quadBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, quadBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(kQuadCorners), kQuadCorners,
                   GL_STATIC_DRAW);

    // The two instance buffers, one for each kind of particle. They are as
    // big as the whole pool, so a frame never has to ask for more.
    const GLsizeiptr bytes = (GLsizeiptr)((size_t)kMaxParticles * kInstanceFloats * sizeof(float));
    gl::GenBuffers(1, &alphaBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, alphaBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
    gl::GenBuffers(1, &additiveBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, additiveBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);

    alphaData_.reserve((size_t)kMaxParticles * kInstanceFloats);
    additiveData_.reserve((size_t)kMaxParticles * kInstanceFloats);
}

void ParticleSystem::release() {
    if (gl::DeleteBuffers) {
        if (quadBuffer_) gl::DeleteBuffers(1, &quadBuffer_);
        if (alphaBuffer_) gl::DeleteBuffers(1, &alphaBuffer_);
        if (additiveBuffer_) gl::DeleteBuffers(1, &additiveBuffer_);
    }
    quadBuffer_ = 0;
    alphaBuffer_ = 0;
    additiveBuffer_ = 0;
    // The program is not thrown away here: it is the program of this class
    // and it goes when the class does. A load() over a program that is
    // already there replaces it and frees the old one.
}

int ParticleSystem::addEmitter(const Emitter& emitter) {
    emitters_.push_back(emitter);
    return (int)emitters_.size() - 1;
}

void ParticleSystem::removeEmitter(int index) {
    if (index < 0 || index >= (int)emitters_.size()) return;
    emitters_.erase(emitters_.begin() + index);
}

void ParticleSystem::clearEmitters() { emitters_.clear(); }

void ParticleSystem::removeBody(int index) {
    for (Emitter& emitter : emitters_) {
        if (emitter.body == index) {
            // The body it rode on is gone: it stands where it is now.
            emitter.body = -1;
        } else if (emitter.body > index) {
            // The bodies after the one that is gone have moved down.
            --emitter.body;
        }
    }
}

void ParticleSystem::removeAllBodies() {
    for (Emitter& emitter : emitters_) emitter.body = -1;
}

int ParticleSystem::emit(const ParticleSettings& settings, const Vec3& position,
                          const Vec3& direction, int count) {
    Emitter emitter;
    emitter.position = position;
    emitter.direction = direction;
    emitter.settings = settings;
    if (count > 0) emitter.settings.count = count;

    // A burst that is not an emitter: it is made now, and it is not in the
    // scene. The salt keeps it apart from the bursts the emitters make, so
    // the two do not look like each other.
    const unsigned salt = seed_ ^ 0x5bf03635u ^ ((unsigned)liveCount_ * 2654435761u);
    const int made = emitter.settings.count > 0 ? emitter.settings.count : 1;
    burst(emitter, position, direction, salt);
    return made;
}

int ParticleSystem::emit(ParticlePreset preset, const Vec3& position, const Vec3& direction,
                          int count) {
    return emit(particlePresetSettings(preset), position, direction, count);
}

Particle& ParticleSystem::take() {
    Particle& particle = particles_[(size_t)nextParticle_];
    // The pool is a ring: a new particle takes the place of the oldest one,
    // and a particle that was already dead is counted as a new one.
    if (!particle.alive) ++liveCount_;
    nextParticle_ = (nextParticle_ + 1) % kMaxParticles;
    return particle;
}

void ParticleSystem::burst(const Emitter& emitter, const Vec3& position, const Vec3& direction,
                           unsigned salt) {
    const ParticleSettings& settings = emitter.settings;
    const int count = settings.count > 0 ? settings.count : 1;

    // Every burst has its own random numbers, and the same burst has the same
    // ones on every machine: the seed of the effect, the emitter it came
    // from and how many bursts that emitter has made.
    Random random(seed_ ^ salt);

    for (int i = 0; i < count; ++i) {
        Particle& particle = take();

        particle.position = position;
        const Vec3 way = random.cone(direction, settings.spread);
        const float speed = std::fmax(0.0f, settings.speed +
                                                random.range(-settings.speedJitter,
                                                             settings.speedJitter));
        particle.velocity = way * speed;

        // The recipe is copied into the particle: an emitter whose recipe is
        // changed does not change the particles that are already in the air.
        particle.gravity = settings.gravity;
        particle.drag = settings.drag;
        particle.colourStart = settings.colour;
        particle.colourEnd = settings.colourEnd;
        particle.size =
            std::fmax(0.001f, settings.size + random.range(-settings.sizeJitter,
                                                            settings.sizeJitter));
        particle.sizeEnd = settings.sizeEnd > 0.0f ? settings.sizeEnd : particle.size;
        particle.alpha = settings.alpha;
        particle.alphaEnd = settings.alphaEnd;
        particle.lifetime =
            std::fmax(0.05f, settings.lifetime + random.range(-settings.lifetimeJitter,
                                                               settings.lifetimeJitter));
        particle.age = 0.0f;
        particle.blend = settings.blend;
        particle.alive = true;
    }
}

void ParticleSystem::clearParticles() {
    for (Particle& particle : particles_) particle.alive = false;
    liveCount_ = 0;
    nextParticle_ = 0;
}

void ParticleSystem::update(float dt, const PhysicsWorld* world) {
    if (dt < 0.0f) dt = 0.0f;

    // The emitters first: the particles they make are in the air before the
    // air is moved, so a particle is born where it was made and not one step
    // further on.
    for (size_t i = 0; i < emitters_.size(); ++i) {
        Emitter& emitter = emitters_[i];
        if (!emitter.enabled) continue;

        Vec3 position = emitter.position;
        Vec3 direction = emitter.direction;
        if (emitter.body >= 0 && world) {
            const RigidBody* body = world->find(emitter.body);
            if (body) {
                // The emitter is in the body own frame: it turns with the
                // body, and a nozzle on the back of a thing sprays out of
                // the back of the thing.
                position = body->position + rotate(body->orientation, emitter.position);
                direction = rotate(body->orientation, emitter.direction);
            }
        }

        const unsigned salt = (unsigned)i * 0x9e3779b9u + (unsigned)emitter.bursts * 0x85ebca6bu;

        if (!emitter.continuous) {
            // One burst, when the emitter is switched on, and then it is
            // done.
            if (!emitter.fired) {
                emitter.fired = true;
                ++emitter.bursts;
                burst(emitter, position, direction, salt);
            }
            continue;
        }

        // And then one every so often.
        emitter.timer -= dt;
        const float interval = emitter.interval > 0.05f ? emitter.interval : 0.05f;
        int made = 0;
        while (emitter.timer <= 0.0f && made < kMaxBurstsPerFrame) {
            emitter.timer += interval;
            ++emitter.bursts;
            ++made;
            burst(emitter, position, direction, salt);
        }
    }

    // The particles in the air.
    for (Particle& particle : particles_) {
        if (!particle.alive) continue;
        particle.age += dt;
        if (particle.age >= particle.lifetime) {
            particle.alive = false;
            --liveCount_;
            continue;
        }

        // The gravity of its own, and then the air taking the speed out of
        // it. The drag is the honest one - the speed is divided by one plus
        // the drag - so a particle that is dragged hard does not overshoot
        // and turn back the way it came.
        particle.velocity += particle.gravity * dt;
        if (particle.drag > 0.0f) particle.velocity *= 1.0f / (1.0f + particle.drag * dt);
        particle.position += particle.velocity * dt;
    }
}

void ParticleSystem::draw(const Camera& camera, const Mat4& view, const Mat4& projection,
                           const FogSettings* fog, const Vec3& sunDirection,
                           const Vec3& sunColour) {
    if (!program_.ready() || liveCount_ == 0) return;
    if (!gl::DrawArraysInstanced || !alphaBuffer_ || !additiveBuffer_) return;

    // The data of the two passes, filled afresh every frame: where the
    // particle is now, how big it is now and what colour it is now.
    alphaData_.clear();
    additiveData_.clear();
    for (const Particle& particle : particles_) {
        if (!particle.alive) continue;

        const float t = particle.lifetime > 0.0f
                            ? clampf(particle.age / particle.lifetime, 0.0f, 1.0f)
                            : 1.0f;
        const float size = lerpf(particle.size, particle.sizeEnd, t);
        const float alpha = lerpf(particle.alpha, particle.alphaEnd, t);
        if (alpha <= 0.002f || size <= 0.0001f) continue;

        const float colour[3] = {
            lerpf(particle.colourStart.x, particle.colourEnd.x, t),
            lerpf(particle.colourStart.y, particle.colourEnd.y, t),
            lerpf(particle.colourStart.z, particle.colourEnd.z, t)};

        std::vector<float>& data = particle.blend == ParticleBlend::Additive ? additiveData_
                                                                             : alphaData_;
        data.push_back(particle.position.x);
        data.push_back(particle.position.y);
        data.push_back(particle.position.z);
        data.push_back(size);
        data.push_back(alpha);
        data.push_back(colour[0]);
        data.push_back(colour[1]);
        data.push_back(colour[2]);
    }

    if (alphaData_.empty() && additiveData_.empty()) return;

    program_.use();
    program_.setMat4("uView", view.m);
    program_.setMat4("uProjection", projection.m);
    // The camera own axes: the quad is turned to face the eye, and it is
    // turned with the camera and not with the world.
    const Vec3 right = camera.right();
    const Vec3 up = camera.up();
    program_.setVec3("uCameraRight", right.x, right.y, right.z);
    program_.setVec3("uCameraUp", up.x, up.y, up.z);
    program_.setVec3("uCameraPosition", camera.position.x, camera.position.y,
                     camera.position.z);

    // The fog: the same air as everything else, and the same numbers.
    //
    // The sun goes with it: the fog is lit by the sun, and the light of the
    // sun is what makes the air in front of a particle bright on the side of
    // the sun and grey on the other side.
    program_.setVec3("uSunDirection", sunDirection.x, sunDirection.y, sunDirection.z);
    program_.setVec3("uSunColour", sunColour.x, sunColour.y, sunColour.z);
    if (fog) {
        program_.setVec3("uFogColour", fog->colour.x, fog->colour.y, fog->colour.z);
        program_.setFloat("uFogDensity", fog->density);
        program_.setFloat("uFogFalloff", fog->falloff);
        program_.setFloat("uFogHeight", fog->height);
        program_.setFloat("uFogScatter", fog->scatter);
        program_.setFloat("uFogForward", fog->forward);
    } else {
        program_.setFloat("uFogDensity", 0.0f);
    }

    // The four corners of the quad. They are the same for every particle, so
    // they are in a buffer of their own and the divisor is zero.
    gl::BindBuffer(GL_ARRAY_BUFFER, quadBuffer_);
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(float),
                             (const void*)0);
    gl::VertexAttribDivisor(3, 0);

    // The particles do not write into the depth: a puff of smoke that hides
    // the puff of smoke behind it is a puff of smoke in front of it, and the
    // order they are drawn in is not the order they are in.
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    program_.setInt("uAdditive", 0);
    drawPass(alphaBuffer_, alphaData_, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // The additive pass: the particles that are added to the picture take
    // the fog differently, and the fog stage is told which kind it is
    // drawing.
    program_.setInt("uAdditive", 1);
    drawPass(additiveBuffer_, additiveData_, GL_SRC_ALPHA, GL_ONE);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // The attributes off again, and the divisors back to zero. A divisor
    // that is left on an attribute is a divisor the next thing that draws
    // inherits, and the fixed pipeline draws the grid and the gizmo with the
    // very same attributes.
    gl::DisableVertexAttribArray(0);
    gl::DisableVertexAttribArray(1);
    gl::DisableVertexAttribArray(2);
    gl::DisableVertexAttribArray(3);
    gl::VertexAttribDivisor(0, 0);
    gl::VertexAttribDivisor(1, 0);
    gl::VertexAttribDivisor(2, 0);
    gl::VertexAttribDivisor(3, 0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::UseProgram(0);
}

} // namespace s3d
