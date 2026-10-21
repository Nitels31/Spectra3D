#include "audio/SoundDevice.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <cstring>
#include <unordered_map>

// miniaudio is a single-header library and its implementation lives here -
// exactly one translation unit may have it.
//
// The layer's own header does not include it at all, and that is not an
// accident of taste: miniaudio's types are anonymous typedefs, so they cannot
// be forward declared, and a header that included four megabytes of miniaudio
// would be included by everything that so much as mentions a sound. The
// device is behind a pointer and only this file ever sees what is behind it.
#define MA_IMPLEMENTATION
#include <miniaudio.h>

namespace s3d {

// How many times the same sound can be heard at once. Two bodies hitting the
// floor in the same frame are two sounds, not one sound played twice, and
// three is enough for that without every sound in the game holding a copy of
// itself in memory.
constexpr int kVoicesPerSound = 3;

// A volume is a multiplier, so a sound may be louder than the master volume
// and still be told to be quiet. Above four it is not a mix any more, it is a
// mistake, and a mistake should not be a distortion.
float clampVolume(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 4.0f) return 4.0f;
    return value;
}

// The sounds of one file: a few instances of it, and which one comes next.
struct Voice {
    ma_sound sound{};
    bool ready = false;
};

struct SoundEntry {
    std::vector<Voice> voices;
    size_t next = 0;
    std::string path;
};

struct SoundDevice::Impl {
    ma_engine engine{};
    bool engineReady = false;
    std::unordered_map<std::string, SoundEntry> sounds;

    ~Impl() {
        for (auto& pair : sounds)
            for (Voice& voice : pair.second.voices)
                if (voice.ready) ma_sound_uninit(&voice.sound);
        if (engineReady) ma_engine_uninit(&engine);
    }
};

SoundDevice::SoundDevice() = default;

SoundDevice::~SoundDevice() { shutdown(); }

bool SoundDevice::start() {
    if (impl_) return true;

    auto impl = std::make_unique<Impl>();
    const ma_result result = ma_engine_init(nullptr, &impl->engine);
    if (result != MA_SUCCESS) {
        Log::warn("Audio: no engine (%s)", ma_result_description(result));
        return false;
    }
    impl->engineReady = true;

    if (ma_engine_start(&impl->engine) != MA_SUCCESS) {
        Log::warn("Audio: the engine did not start");
        ma_engine_uninit(&impl->engine);
        impl->engineReady = false;
        return false;
    }

    // The device the engine made for itself, and its name: the panel in the
    // editor shows it, because "no sound" and "the wrong sound" are very
    // different problems and the name says which one it is.
    ma_device* device = ma_engine_get_device(&impl->engine);
    if (device) {
        char name[256] = {};
        ma_device_get_name(device, ma_device_type_playback, name, sizeof(name), nullptr);
        deviceName_ = name;
    }
    if (deviceName_.empty()) deviceName_ = "default";

    impl_ = std::move(impl);
    Log::info("Audio: device '%s'", deviceName_.c_str());
    return true;
}

void SoundDevice::shutdown() {
    if (!impl_) return;
    impl_.reset();
    deviceName_.clear();
    names_.clear();
}

bool SoundDevice::running() const { return impl_ != nullptr; }

bool SoundDevice::load(const std::string& name, const std::string& path) {
    if (!impl_) {
        Log::warn("Audio: no device, cannot load '%s'", name.c_str());
        return false;
    }
    if (!fs::exists(path)) {
        Log::warn("Audio: '%s' is not there", path.c_str());
        return false;
    }

    // A sound that is already loaded is replaced: the file on disk may have
    // changed, and a reload that keeps the old copy is a reload that did
    // nothing.
    SoundEntry entry;
    entry.path = path;
    entry.voices.resize(kVoicesPerSound);

    for (Voice& voice : entry.voices) {
        // The device can only be given a narrow path, so the path is handed over
        // in the machine's own code page: a sound in a folder the machine
        // cannot spell is a sound that is not there.
        const ma_result result = ma_sound_init_from_file(
            &impl_->engine, fs::systemPath(path).c_str(), 0, nullptr, nullptr, &voice.sound);
        if (result != MA_SUCCESS) {
            Log::warn("Audio: cannot read '%s': %s", path.c_str(),
                      ma_result_description(result));
            for (Voice& done : entry.voices)
                if (done.ready) ma_sound_uninit(&done.sound);
            return false;
        }
        voice.ready = true;
    }

    const bool isNew = impl_->sounds.find(name) == impl_->sounds.end();
    impl_->sounds[name] = std::move(entry);
    if (isNew) names_.push_back(name);
    return true;
}

int SoundDevice::loadFolder(const std::string& directory) {
    if (!fs::isDirectory(directory)) return 0;

    int loaded = 0;
    for (const std::string& file : fs::listFilesByExtension(directory, {".wav", ".ogg", ".mp3",
                                                                        ".flac"})) {
        // The name of a sound is the name of its file without the extension, so
        // a file dropped into the folder is a sound the scripts can play.
        if (load(str::fileStem(file), file)) ++loaded;
    }
    return loaded;
}

bool SoundDevice::has(const std::string& name) const {
    return impl_ && impl_->sounds.find(name) != impl_->sounds.end();
}

const std::vector<std::string>& SoundDevice::names() const { return names_; }

bool SoundDevice::play(const std::string& name, float volume, float pitch, bool looping) {
    if (!impl_) return false;
    const auto found = impl_->sounds.find(name);
    if (found == impl_->sounds.end()) {
        Log::warn("Audio: no sound called '%s'", name.c_str());
        return false;
    }

    SoundEntry& entry = found->second;
    Voice& voice = entry.voices[entry.next];
    entry.next = (entry.next + 1) % entry.voices.size();
    if (!voice.ready) return false;

    ma_sound_set_volume(&voice.sound, clampVolume(volume));
    ma_sound_set_pitch(&voice.sound, pitch > 0.01f ? pitch : 1.0f);
    ma_sound_set_looping(&voice.sound, looping ? MA_TRUE : MA_FALSE);
    // A sound that is already playing starts again from the beginning. The
    // sound is not rewound when it stops, so it has to be rewound here.
    ma_sound_seek_to_pcm_frame(&voice.sound, 0);
    return ma_sound_start(&voice.sound) == MA_SUCCESS;
}

void SoundDevice::stop(const std::string& name) {
    if (!impl_) return;
    const auto found = impl_->sounds.find(name);
    if (found == impl_->sounds.end()) return;
    for (Voice& voice : found->second.voices)
        if (voice.ready) ma_sound_stop(&voice.sound);
}

void SoundDevice::stopAll() {
    if (!impl_) return;
    for (auto& pair : impl_->sounds)
        for (Voice& voice : pair.second.voices)
            if (voice.ready) ma_sound_stop(&voice.sound);
}

bool SoundDevice::isPlaying(const std::string& name) const {
    if (!impl_) return false;
    const auto found = impl_->sounds.find(name);
    if (found == impl_->sounds.end()) return false;
    for (const Voice& voice : found->second.voices)
        if (voice.ready && ma_sound_is_playing(&voice.sound)) return true;
    return false;
}

void SoundDevice::setMasterVolume(float volume) {
    masterVolume_ = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (impl_) ma_engine_set_volume(&impl_->engine, masterVolume_);
}

} // namespace s3d
