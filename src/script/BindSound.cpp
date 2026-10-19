#include "script/Bindings.hpp"

#include "audio/SoundDevice.hpp"
#include "core/Log.hpp"
#include "physics/RigidBody.hpp"
#include "script/BindString.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// --- engine:: sound -----------------------------------------------------------
//
// The sounds are named, not paths: a script says `engine::playSound("hit")`
// and the device knows which file that is. The same name is what a body
// carries in the scene, so a sound can be given to a body by a script as well
// as by the inspector.
//
// Every binding here answers quietly when there is no device. A machine with
// no sound card is a machine where the game still has to run, and a script
// that cannot play a sound has nothing to do about it.

SoundDevice* soundDevice() {
    ScriptState* state = activeState();
    return state ? state->sound : nullptr;
}

bool playSound(const ScriptString& name) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->play(name.str());
}

bool playSoundLoop(const ScriptString& name) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->play(name.str(), 1.0f, 1.0f, true);
}

bool playSoundWith(const ScriptString& name, float volume, float pitch) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->play(name.str(), volume, pitch, false);
}

void stopSound(const ScriptString& name) {
    SoundDevice* device = soundDevice();
    if (!device) return;
    device->stop(name.str());
}

void stopAllSounds() {
    SoundDevice* device = soundDevice();
    if (!device) return;
    device->stopAll();
}

bool isSoundPlaying(const ScriptString& name) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->isPlaying(name.str());
}

bool hasSound(const ScriptString& name) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->has(name.str());
}

void setMasterVolume(float volume) {
    SoundDevice* device = soundDevice();
    if (!device) return;
    device->setMasterVolume(volume);
}

float getMasterVolume() {
    SoundDevice* device = soundDevice();
    return device ? device->masterVolume() : 0.0f;
}

// Loads a sound from a file, under a name. The path is the one the engine
// knows: a file next to the program, or in the assets folder.
bool loadSound(const ScriptString& name, const ScriptString& path) {
    SoundDevice* device = soundDevice();
    if (!device) return false;
    return device->load(name.str(), path.str());
}

// The sound a body makes when it hits something. Empty means "no sound", which
// is the same thing the inspector says.
void setBodySound(int index, const ScriptString& name) {
    RigidBody* body = resolveBody("setBodySound", index);
    if (!body) return;
    body->sound = name.str();
}

bool bodyHasSound(int index) {
    RigidBody* body = resolveBody("bodyHasSound", index);
    return body && !body->sound.empty();
}

const Binding kBindings[] = {
    {"bool playSound(const string &in)", asFUNCTION(playSound)},
    {"bool playSoundLoop(const string &in)", asFUNCTION(playSoundLoop)},
    {"bool playSoundWith(const string &in, float volume, float pitch)",
     asFUNCTION(playSoundWith)},
    {"void stopSound(const string &in)", asFUNCTION(stopSound)},
    {"void stopAllSounds()", asFUNCTION(stopAllSounds)},
    {"bool isSoundPlaying(const string &in)", asFUNCTION(isSoundPlaying)},
    {"bool hasSound(const string &in)", asFUNCTION(hasSound)},
    {"void setMasterVolume(float volume)", asFUNCTION(setMasterVolume)},
    {"float getMasterVolume()", asFUNCTION(getMasterVolume)},
    {"bool loadSound(const string &in, const string &in)", asFUNCTION(loadSound)},
    {"void setBodySound(int index, const string &in)", asFUNCTION(setBodySound)},
    {"bool bodyHasSound(int index)", asFUNCTION(bodyHasSound)},
};

} // namespace

int registerSoundBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif  // SPECTRA_SCRIPTING
