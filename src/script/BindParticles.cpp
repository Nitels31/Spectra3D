#include "script/Bindings.hpp"

#include "core/Log.hpp"
#include "render/ParticleSystem.hpp"
#include "script/BindString.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// --- engine:: particles ------------------------------------------------------
//
// The effects of the world, asked for by name.
//
// The names are the same ones the editor uses and the same ones the scene
// file uses: "fire", "smoke", "spark", "dust", "explosion". A script that
// asks for a name the engine does not have is told so in the log, and the
// effect that is asked for is not made - a script that asks for a fire and
// gets nothing is a bug in the script, and a log line is where such a bug
// is found.
//
// Every binding here answers quietly when there is no particle system. A
// machine that has no particle system is a machine where the game still has
// to run.

ParticleSystem* particles() {
    ScriptState* state = activeState();
    return state ? state->particles : nullptr;
}

// The settings of a named effect, or nothing when the name is not one of
// them.
bool settingsFor(const char* fn, const ScriptString& name, ParticleSettings& out) {
    ParticlePreset preset;
    if (!particlePresetFromName(name.str(), preset)) {
        Log::error("engine::%s: '%s' is not an effect the engine knows", fn, name.c_str());
        return false;
    }
    out = particlePresetSettings(preset);
    return true;
}

void emitParticles(float x, float y, float z, int count) {
    ParticleSystem* system = particles();
    if (!system) return;
    system->emit(ParticlePreset::Fire, Vec3{x, y, z}, Vec3{0.0f, 1.0f, 0.0f}, count);
}

void emitParticlesNamed(float x, float y, float z, int count, const ScriptString& preset) {
    ParticleSystem* system = particles();
    if (!system) return;
    ParticleSettings settings;
    if (!settingsFor("emitParticles", preset, settings)) return;
    system->emit(settings, Vec3{x, y, z}, Vec3{0.0f, 1.0f, 0.0f}, count);
}

// An emitter in the world: a place that keeps making the effect, once a
// second, for as long as the scene is there.
int addEmitter(float x, float y, float z, const ScriptString& preset) {
    ParticleSystem* system = particles();
    if (!system) return -1;
    ParticleSettings settings;
    if (!settingsFor("addEmitter", preset, settings)) return -1;

    Emitter emitter;
    emitter.position = Vec3{x, y, z};
    emitter.direction = Vec3{0.0f, 1.0f, 0.0f};
    emitter.settings = settings;
    emitter.continuous = true;
    emitter.interval = 1.0f;
    return system->addEmitter(emitter);
}

void removeEmitter(int index) {
    ParticleSystem* system = particles();
    if (!system) return;
    system->removeEmitter(index);
}

void clearParticles() {
    ParticleSystem* system = particles();
    if (!system) return;
    system->clearParticles();
}

const Binding kBindings[] = {
    {"void emitParticles(float x, float y, float z, int count)", asFUNCTION(emitParticles)},
    {"void emitParticles(float x, float y, float z, int count, const string &in preset)",
     asFUNCTION(emitParticlesNamed)},
    {"int addEmitter(float x, float y, float z, const string &in preset)", asFUNCTION(addEmitter)},
    {"void removeEmitter(int index)", asFUNCTION(removeEmitter)},
    {"void clearParticles()", asFUNCTION(clearParticles)},
};

} // namespace

int registerParticleBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif  // SPECTRA_SCRIPTING
