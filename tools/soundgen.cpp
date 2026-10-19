// soundgen - the sounds the engine ships.
//
// The engine has no audio of its own: a sound is a file, and the file is an
// asset. This is where the test sounds come from - three short pieces, two
// seconds each, written as 16 bit PCM WAV so that anything can read them.
//
// They are made to be told apart by ear, because a sound system is judged by
// what it plays and not by what the log says:
//
//   tone.wav  - a steady note with a slow tremolo. The reference: if the
//               tremolo is audible, the mixer is running.
//   sweep.wav - a chirp from low to high. The one that says whether the
//               pitch is right: a sweep that goes down is the wrong way
//               round.
//   thud.wav  - a noise burst over a low note. The one that says whether
//               short sounds are short: a thud that rings on is a thud with
//               a tail.
//
// Usage:
//   soundgen [outdir]
//
// The default is assets/sounds, which is where the engine looks for them.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kSampleRate = 44100;
constexpr double kPi = 3.14159265358979323846;

// A tiny deterministic noise, so the same file comes out every time: a thud
// that is different on every run cannot be compared with the last one.
float noise(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return ((seed >> 8) / 8388608.0f) - 1.0f;  // -1..1
}

float envelope(float t, float seconds) {
    // A short attack, a slow release: a sound that starts at full volume
    // clicks, and a sound that never fades off is cut off.
    const float attack = std::fmin(1.0f, t / 0.01f);
    const float release = std::fmin(1.0f, (seconds - t) / 0.15f);
    return attack * release;
}

// Writes a 16 bit mono PCM WAV. The header is written by hand rather than
// with a library because there is no library here to use, and a WAV header is
// four short fields and a size.
bool writeWav(const std::string& path, const std::vector<float>& samples) {
    const uint32_t dataBytes = (uint32_t)(samples.size() * 2);
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;

    auto u32 = [&](uint32_t value) { std::fwrite(&value, 4, 1, file); };
    auto u16 = [&](uint16_t value) { std::fwrite(&value, 2, 1, file); };
    auto tag = [&](const char* text) { std::fwrite(text, 1, 4, file); };

    tag("RIFF");
    u32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    u32(16);                                       // the size of this chunk
    u16(1);                                        // PCM
    u16(1);                                        // one channel
    u32(kSampleRate);
    u32((uint32_t)kSampleRate * 2);                // bytes a second
    u16(2);                                        // a frame is two bytes
    u16(16);                                       // bits a sample
    tag("data");
    u32(dataBytes);

    for (float sample : samples) {
        const float clamped = sample < -1.0f ? -1.0f : (sample > 1.0f ? 1.0f : sample);
        const int16_t pcm = (int16_t)(clamped * 32767.0f);
        std::fwrite(&pcm, 2, 1, file);
    }
    std::fclose(file);
    return true;
}

// A steady note with a tremolo on it.
std::vector<float> makeTone(double seconds) {
    const size_t count = (size_t)(seconds * kSampleRate);
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; ++i) {
        const float t = (float)i / (float)kSampleRate;
        const float tremolo = 0.65f + 0.35f * std::sin(2.0f * (float)kPi * 3.0f * t);
        samples[i] = 0.45f * tremolo * std::sin(2.0f * (float)kPi * 440.0f * t) *
                     envelope(t, (float)seconds);
    }
    return samples;
}

// A chirp: the pitch climbs from 220 Hz to 1760 Hz over the whole piece.
std::vector<float> makeSweep(double seconds) {
    const size_t count = (size_t)(seconds * kSampleRate);
    std::vector<float> samples(count);
    double phase = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const float t = (float)i / (float)kSampleRate;
        const float progress = t / (float)seconds;
        const float frequency = 220.0f * std::pow(8.0f, progress);  // three octaves
        phase += 2.0 * (float)kPi * frequency / (float)kSampleRate;
        samples[i] = 0.45f * std::sin((float)phase) * envelope(t, (float)seconds);
    }
    return samples;
}

// A burst of noise over a low note: the sound of something hitting something.
std::vector<float> makeThud(double seconds) {
    const size_t count = (size_t)(seconds * kSampleRate);
    std::vector<float> samples(count);
    uint32_t seed = 12345u;
    float previous = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float t = (float)i / (float)kSampleRate;
        // One pole low pass over the noise: white noise is a hiss, and a
        // thud is a knock.
        const float raw = noise(seed);
        previous = previous * 0.82f + raw * 0.18f;
        const float body = std::sin(2.0f * (float)kPi * 90.0f * t) * std::exp(-6.0f * t);
        const float knock = previous * std::exp(-14.0f * t);
        samples[i] = 0.55f * (body * 0.6f + knock * 2.2f) * envelope(t, (float)seconds);
    }
    return samples;
}

} // namespace

int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "assets/sounds";
    const double seconds = 2.0;

    // The folder is made if it is not there: a generator that only works into
    // a folder somebody made by hand is a generator with a step in it that
    // nobody remembers.
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::fprintf(stderr, "soundgen: cannot make '%s': %s\n", directory.c_str(),
                     error.message().c_str());
        return 1;
    }

    struct Piece {
        const char* name;
        std::vector<float> samples;
    };
    Piece pieces[3] = {{"tone", makeTone(seconds)},
                        {"sweep", makeSweep(seconds)},
                        {"thud", makeThud(seconds)}};

    int written = 0;
    for (const Piece& piece : pieces) {
        const std::string path = directory + "/" + piece.name + ".wav";
        if (!writeWav(path, piece.samples)) {
            std::fprintf(stderr, "soundgen: cannot write '%s'\n", path.c_str());
            return 1;
        }
        std::printf("soundgen: wrote %s (%.1f s, %d Hz, 16 bit mono)\n", path.c_str(), seconds,
                    kSampleRate);
        ++written;
    }
    std::printf("soundgen: %d file(s) in %s\n", written, directory.c_str());
    return 0;
}
