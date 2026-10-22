#include "editor/Editor.hpp"

#include "core/Math.hpp"
#include "render/ParticleSystem.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kFieldHeight = 24.0f;
constexpr float kButtonHeight = 26.0f;
constexpr float kRowHeight = 22.0f;

// How many emitters the list shows. Two ids to a row - the row and the X
// that takes the emitter away - and the ids of the rows are a stretch of a
// hundred.
constexpr int kMaxEmitterRows = 50;

// The colours an effect can be. The picture is drawn in the colour that is
// chosen, and the colour it dies in is a darker version of it - so a fire
// that is picked in blue is a blue fire and not a red one.
constexpr int kParticleColors = 6;
constexpr float kParticleColorValues[kParticleColors][3] = {
    {1.00f, 0.85f, 0.45f},  // fire
    {0.32f, 0.30f, 0.29f},  // smoke
    {1.00f, 0.92f, 0.55f},  // spark
    {0.62f, 0.55f, 0.42f},  // dust
    {0.45f, 0.75f, 1.00f},  // cold
    {0.85f, 0.40f, 0.90f},  // magic
};

// The name of an emitter, as the list shows it.
//
// An emitter has no name of its own - it is a place and a recipe - so the
// list says what it looks like: the colour of the effect and the number of
// particles in it. Two emitters that look the same are the same effect, and
// a person recognises a fire by its colour and not by its position.
void emitterLabel(char* out, size_t size, const Emitter& emitter) {
    std::snprintf(out, size, "%d at %d, %d, %d", emitter.settings.count,
                  (int)emitter.position.x, (int)emitter.position.y, (int)emitter.position.z);
}

} // namespace

// The name of an effect, in the language of the editor.
//
// The names the engine knows the effects by are English - they are the names
// a script and a scene file use - and the panel shows them translated: the
// key is built once and kept, because the text table wants a pointer and a
// pointer into a local string is a pointer to nothing.
const char* Editor::particlePresetLabel(int index) const {
    static char keys[kParticlePresetCount][32] = {};
    if (index < 0 || index >= kParticlePresetCount) return "";
    if (keys[index][0] == '\0')
        std::snprintf(keys[index], sizeof(keys[index]), "particles.preset.%s",
                      particlePresetName((ParticlePreset)index));
    return text(keys[index]);
}

// The Particles panel: the effects of the world.
//
// It says how one effect is made - how many particles, how fast, how big,
// how long they live - and it lists the emitters that are in the scene. The
// effect is a recipe, and the recipe is a copy of one of the presets that
// the fields then change: two effects made one after the other are not the
// same effect twice.
//
// The emitters are scene data. They go into the scene file with everything
// else, and a scene that is opened again has them.
void Editor::drawParticlesPanel(UiRenderer& ui, const Rect& dock) {
    if (dock.width <= 0.0f || dock.height <= 0.0f) return;

    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, text("tab.particles"), theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);

    // The body scrolls and the bar at the bottom does not: the buttons that
    // make the effect have to be there whatever the size of the window.
    const float barHeight = kButtonHeight * 2.0f + 14.0f + theme::textLineHeight + 4.0f;
    const Rect body{dock.x, dock.y + kHeaderHeight, dock.width,
                    std::fmax(0.0f, dock.height - kHeaderHeight - barHeight)};

    const float maxScroll = std::fmax(0.0f, particleContentHeight_ - body.height);
    particleScroll_ = clampf(particleScroll_, 0.0f, maxScroll);
    if (ui_.hovered(body) && ui_.scrollDelta() != 0.0)
        particleScroll_ = clampf(particleScroll_ - (float)ui_.scrollDelta() * 24.0f, 0.0f,
                                 maxScroll);

    ui.pushClip(body);
    ui_.pushClip(body);

    float y = dock.y + kHeaderHeight + 8.0f - particleScroll_;
    const float width = dock.width - 16.0f;
    const float x = dock.x + 8.0f;

    // --- the effect ---------------------------------------------------------
    ui.textFit(x, y, width, text("particles.effect"), theme::textDim);
    y += theme::textLineHeight + 4.0f;

    // The five effects the engine knows by name, three in a row and then two:
    // the names are words, and words do not fit in a fifth of a panel.
    constexpr int kPerRow = 3;
    const float buttonWidth = (width - 8.0f) / (float)kPerRow;
    for (int i = 0; i < kParticlePresetCount; ++i) {
        const int col = i % kPerRow;
        const int row = i / kPerRow;
        const Rect hit{x + (float)col * (buttonWidth + 4.0f),
                       y + (float)row * (kButtonHeight + 4.0f), buttonWidth, kButtonHeight};
        const bool on = (int)particlePreset_ == i;
        if (ui_.button(ui, kParticlePreset + i, hit.x, hit.y, hit.width, hit.height,
                       particlePresetLabel(i), on)) {
            // A new effect starts as the named one, unchanged: the fields
            // below then change the copy, and the next effect that is picked
            // starts from its own name again.
            particlePreset_ = (ParticlePreset)i;
            particleSettings_ = particlePresetSettings(particlePreset_);
        }
    }
    y += (float)((kParticlePresetCount + kPerRow - 1) / kPerRow) * (kButtonHeight + 4.0f) + 2.0f;

    // --- the numbers of the effect ------------------------------------------
    // The numbers a person uses to describe an effect: how many, how fast,
    // how big, how long, how wide a cone.
    ui_.propertyInt(ui, kParticleCount, x, y, width, text("particles.count"),
                    particleSettings_.count, 1, 400, 0.5f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kParticleSpeed, x, y, width, text("particles.speed"),
                      particleSettings_.speed, 0.05f, 0.0f, 30.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kParticleSize, x, y, width, text("particles.size"),
                      particleSettings_.size, 0.01f, 0.01f, 10.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kParticleLifetime, x, y, width, text("particles.lifetime"),
                      particleSettings_.lifetime, 0.05f, 0.05f, 30.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kParticleSpread, x, y, width, text("particles.spread"),
                      particleSettings_.spread, 1.0f, 0.0f, 180.0f);
    y += kFieldHeight;
    // The pull of the world on the particles, along the up axis. A spark falls
    // and smoke rises, and the difference between them is this number.
    ui_.propertyFloat(ui, kParticleGravity, x, y, width, text("particles.gravity"),
                      particleSettings_.gravity.y, 0.05f, -30.0f, 30.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kParticleDrag, x, y, width, text("particles.drag"),
                      particleSettings_.drag, 0.05f, 0.0f, 10.0f);
    y += kFieldHeight;

    // --- how it is drawn ----------------------------------------------------
    // Over what is behind it, or added to it. Smoke is over and fire is
    // added, and the two are different pictures and not the same picture with
    // a different brightness.
    ui.textFit(x, y, width, text("particles.blend"), theme::textDim);
    y += theme::textLineHeight + 4.0f;
    {
        const float half = (width - 4.0f) * 0.5f;
        const Rect alpha{x, y, half, kButtonHeight};
        const Rect additive{x + half + 4.0f, y, half, kButtonHeight};
        if (ui_.button(ui, kParticleBlend, alpha.x, alpha.y, alpha.width, alpha.height,
                       text("particles.alpha"),
                       particleSettings_.blend == ParticleBlend::Alpha))
            particleSettings_.blend = ParticleBlend::Alpha;
        if (ui_.button(ui, kParticleBlend + 1, additive.x, additive.y, additive.width,
                       additive.height, text("particles.additive"),
                       particleSettings_.blend == ParticleBlend::Additive))
            particleSettings_.blend = ParticleBlend::Additive;
    }
    y += kButtonHeight + 6.0f;

    // The colour of the effect. The colour it dies in is a darker version of
    // it, so a fire that is picked in blue is a blue fire and not a red one.
    ui.textFit(x, y, width, text("particles.colour"), theme::textDim);
    y += theme::textLineHeight + 4.0f;
    {
        const float swatch = std::min(28.0f, (width - 10.0f) / (float)kParticleColors);
        for (int i = 0; i < kParticleColors; ++i) {
            const float sx = x + (float)i * (swatch + 2.0f);
            const Rect hit{sx, y, swatch, swatch};
            if (ui_.button(ui, kParticleColour + i, hit.x, hit.y, hit.width, hit.height, "")) {
                particleSettings_.colour =
                    Vec3{kParticleColorValues[i][0], kParticleColorValues[i][1],
                         kParticleColorValues[i][2]};
                // And the colour it dies in follows the one that is chosen:
                // a darker version of it, so a fire that is picked in blue is
                // a blue fire and not a red one.
                particleSettings_.colourEnd = particleSettings_.colour * 0.3f;
            }
            ui.fillRect(hit.x, hit.y, hit.width, hit.height,
                        Color{kParticleColorValues[i][0], kParticleColorValues[i][1],
                              kParticleColorValues[i][2]});
            // The one that is chosen is the one with a frame around it.
            const Vec3& colour = particleSettings_.colour;
            if (std::fabs(colour.x - kParticleColorValues[i][0]) < 0.01f &&
                std::fabs(colour.y - kParticleColorValues[i][1]) < 0.01f &&
                std::fabs(colour.z - kParticleColorValues[i][2]) < 0.01f) {
                ui.fillRect(hit.x - 2.0f, hit.y - 2.0f, hit.width + 4.0f, 1.0f, theme::accent);
                ui.fillRect(hit.x - 2.0f, hit.y + hit.height + 1.0f, hit.width + 4.0f, 1.0f,
                            theme::accent);
            }
        }
        y += swatch + 8.0f;
    }

    // --- the emitters in the scene -------------------------------------------
    ui.textFit(x, y, width, text("particles.emitters"), theme::textDim);
    y += theme::textLineHeight + 4.0f;

    if (!particles_) {
        ui.textFit(x, y, width, text("particles.none"), theme::textDim);
        y += kRowHeight;
    } else {
        const std::vector<Emitter>& emitters = particles_->emitters();
        if (emitters.empty()) {
            ui.textFit(x, y, width, text("particles.noEmitters"), theme::textDim);
            y += kRowHeight;
        }
        // The list shows as many emitters as there are ids for them: fifty
        // rows, two ids each. The rest are in the scene and the count says
        // how many - the list is a picture of the scene, not the scene.
        const size_t shown = std::min(emitters.size(), (size_t)kMaxEmitterRows);
        for (size_t i = 0; i < shown; ++i) {
            const Rect row{x, y, width, kRowHeight};
            // Two ids to a row: the row and the X that takes the emitter away.
            const int base = kParticleRow + (int)i * 2;
            // The row is what is chosen when it is pressed: the emitter that
            // is worked on is the one the fields above are about.
            if (ui_.button(ui, base, row.x, row.y, row.width - 24.0f, row.height, "",
                           (int)i == particleEmitter_)) {
                particleEmitter_ = (int)i;
            }
            char label[64];
            emitterLabel(label, sizeof(label), emitters[i]);
            ui.text(row.x + 6.0f, row.y + 3.0f, label,
                    (int)i == particleEmitter_ ? theme::text : theme::textDim);
            // Its colour, so a fire is a fire and not a row of numbers.
            ui.fillRect(row.x + row.width - 34.0f, row.y + 5.0f, 12.0f, 12.0f,
                        Color{emitters[i].settings.colour.x, emitters[i].settings.colour.y,
                              emitters[i].settings.colour.z});
            // And the X that takes it away.
            const Rect removeButton{row.x + row.width - 20.0f, row.y + 2.0f, 18.0f, 18.0f};
            if (ui_.button(ui, base + 1, removeButton.x, removeButton.y, removeButton.width,
                           removeButton.height, "x")) {
                particles_->removeEmitter((int)i);
                particleEmitter_ = -1;
                markSceneDirty();
                // The list is shorter now, and the rows after this one have
                // moved up: the frame is drawn again next time.
                break;
            }
            y += kRowHeight;
        }
        if (emitters.size() > shown) {
            // The number and the word, in two pieces: the number is a number
            // in every language and the word is a word in this one.
            char more[32];
            std::snprintf(more, sizeof(more), "+%d", (int)(emitters.size() - shown));
            ui.text(x, y, more, theme::textDim);
            ui.text(x + 30.0f, y, text("particles.more"), theme::textDim);
            y += kRowHeight;
        }
    }

    // How tall the panel wanted to be, measured on the way out.
    particleContentHeight_ = y + particleScroll_ - body.y + 8.0f;

    ui_.popClip();
    ui.popClip();

    // --- the bar -------------------------------------------------------------
    // The four buttons, two in a row: a burst now, an emitter in the scene,
    // an emitter that rides on the body that is selected, and the emitter
    // that is selected taken away.
    const float barY = dock.y + dock.height - barHeight;
    ui.fillRect(dock.x, barY, dock.width, 1.0f, theme::separator);
    const float half = (dock.width - 20.0f) * 0.5f;
    const Rect emit{dock.x + 8.0f, barY + 6.0f, half, kButtonHeight};
    const Rect add{emit.x + half + 4.0f, emit.y, half, kButtonHeight};
    const Rect follow{dock.x + 8.0f, emit.y + kButtonHeight + 4.0f, half, kButtonHeight};
    const Rect removeButton{follow.x + half + 4.0f, follow.y, half, kButtonHeight};

    const bool canEmit = particles_ != nullptr;
    if (ui_.button(ui, kParticleEmit, emit.x, emit.y, emit.width, emit.height,
                   text("particles.emit")) && canEmit) {
        // A burst where the eye is, going the way the eye is looking: the
        // only place a person can say "here, like that".
        particles_->emit(particleSettings_, frameCamera_.position, frameCamera_.forward());
    }
    if (ui_.button(ui, kParticleAdd, add.x, add.y, add.width, add.height,
                   text("particles.add")) && canEmit) {
        Emitter emitter;
        emitter.position = frameCamera_.position;
        emitter.direction = frameCamera_.forward();
        emitter.settings = particleSettings_;
        // It keeps making the effect: an emitter that made it once and was
        // done would be a burst, and a burst is what the button above is for.
        emitter.continuous = true;
        emitter.interval = 1.0f;
        particles_->addEmitter(emitter);
        particleEmitter_ = (int)particles_->emitters().size() - 1;
        markSceneDirty();
    }
    if (ui_.button(ui, kParticleFollow, follow.x, follow.y, follow.width, follow.height,
                   text("particles.follow")) && canEmit && !selectedBodies_.empty()) {
        // An emitter that rides on a body: the position is in the body own
        // frame, so it turns with the body and stays where it is on it.
        Emitter emitter;
        emitter.body = selectedBodies_.front();
        emitter.position = Vec3{0.0f, 0.0f, 0.0f};
        emitter.direction = Vec3{0.0f, 1.0f, 0.0f};
        emitter.settings = particleSettings_;
        emitter.continuous = true;
        emitter.interval = 1.0f;
        particles_->addEmitter(emitter);
        particleEmitter_ = (int)particles_->emitters().size() - 1;
        markSceneDirty();
    }
    if (ui_.button(ui, kParticleRemove, removeButton.x, removeButton.y, removeButton.width,
                   removeButton.height, text("particles.remove")) && canEmit &&
        particleEmitter_ >= 0) {
        particles_->removeEmitter(particleEmitter_);
        particleEmitter_ = -1;
        markSceneDirty();
    }
    ui.textFit(dock.x + 8.0f, removeButton.y + kButtonHeight + 4.0f, dock.width - 16.0f,
               text("particles.hint"), theme::textDim);
}

} // namespace s3d
