#pragma once
#include <memory>
#include <string>
#include <vector>

namespace s3d {

// The sound of the world: a device, the sounds loaded into it, and the playing
// of them.
//
// The device is an instance and not a global. The engine makes no sound until
// somebody starts it, and a machine with no sound card - or a container with
// no sound card - is a machine where everything else still has to work, so a
// failure to start is a line in the log and not a reason to stop.
//
// The sounds are named, not paths. A script says `engine::playSound("hit")`
// and the registry knows which file that is; the same name is what a body
// carries in the scene, so a sound can be given to a body without the physics
// layer ever hearing a file name.
class SoundDevice {
public:
    ~SoundDevice();
    // Defined in the .cpp, not defaulted here: a defaulted constructor still
    // has to be able to destroy the members if it throws, and destroying the
    // pointer needs the type behind it - which is only complete in the file
    // that knows what is behind it.
    SoundDevice();
    SoundDevice(const SoundDevice&) = delete;
    SoundDevice& operator=(const SoundDevice&) = delete;

    // Starts the device. Returns false when there is no device to start: the
    // sound turned off in the hardware, a machine with no sound card, a
    // server with no audio at all. The engine carries on without a sound.
    bool start();
    void shutdown();
    bool running() const;

    // Loads a sound from a file. `name` is what it is played by.
    bool load(const std::string& name, const std::string& path);

    // Loads every sound in a folder: the name of each is its file's stem, so
    // a file dropped into assets/sounds/ is a sound the scripts can play
    // without anything else being written. Returns how many were loaded.
    int loadFolder(const std::string& directory);

    bool has(const std::string& name) const;
    // The names of the loaded sounds, in the order they were loaded.
    const std::vector<std::string>& names() const;

    // Plays a sound. `volume` and `pitch` multiply the sound's own loudness
    // and speed, and `looping` says whether it plays again when it runs out.
    //
    // A sound that is already playing is played again from the beginning: the
    // same sound twice is the same sound twice, not a half of one sound and a
    // half of another.
    bool play(const std::string& name, float volume = 1.0f, float pitch = 1.0f,
              bool looping = false);
    void stop(const std::string& name);
    void stopAll();
    bool isPlaying(const std::string& name) const;

    // The loudness of everything the device plays, 0..1.
    void setMasterVolume(float volume);
    float masterVolume() const { return masterVolume_; }

    // The device's own name, for the log and for the editor's panel.
    const std::string& deviceName() const { return deviceName_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    float masterVolume_ = 1.0f;
    std::string deviceName_;
    std::vector<std::string> names_;
};

} // namespace s3d
