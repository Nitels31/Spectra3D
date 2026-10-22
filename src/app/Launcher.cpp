#include "app/Launcher.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

// --- metrics ---------------------------------------------------------------
// Full-bleed layout, as in the concept sketch: the tab strip touches the top
// edge, the sidebar touches the left edge, and the project list takes the rest.
// Everything is a fixed number of pixels and every panel is clipped to the
// framebuffer, so a window smaller than the design size degrades into fewer
// visible rows instead of geometry off the edge of the screen.

constexpr float kPadding = 12.0f;
constexpr float kGap = 8.0f;
constexpr float kTopBarHeight = 46.0f;
constexpr float kSidebarWidth = 212.0f;
constexpr float kFooterHeight = 30.0f;

// A project row is taller than the editor's 22 pixel row: it carries a name and
// a path on two lines.
constexpr float kRowHeight = 44.0f;
constexpr float kRowIcon = 26.0f;
constexpr float kFieldHeight = 22.0f;
constexpr float kTextHeight = theme::textLineHeight;
constexpr float kNavHeight = 28.0f;

// UiContext::sectionHeader draws a fixed 22 pixel strip; the body starts here.
constexpr float kSectionHeader = 22.0f;
constexpr float kContentInset = 8.0f;

constexpr float kMinButtonWidth = 60.0f;
constexpr float kButtonWidth = 84.0f;
constexpr float kDeleteButtonWidth = 74.0f;
constexpr float kStarSize = 20.0f;
// The settings icon dropdown is one of these, so the list cannot be longer than
// the array the draw pass hands out. A folder with more than this many icons is
// still usable - the extras are simply not offered.
constexpr int kMaxIconItems = 32;

// Rows reserved at the bottom of the project list for the hint row.
constexpr float kListOverhead = kSectionHeader + kContentInset + kContentInset;

// --- small path helpers ----------------------------------------------------

// Clamps a rectangle to the framebuffer. Every panel goes through this, so a
// window smaller than the design size can never make the batcher emit a
// rectangle outside the viewport.
Rect intersect(const Rect& rect, float frameWidth, float frameHeight) {
    const float x0 = std::fmax(0.0f, rect.x);
    const float y0 = std::fmax(0.0f, rect.y);
    const float x1 = std::fmin(frameWidth, rect.x + std::fmax(0.0f, rect.width));
    const float y1 = std::fmin(frameHeight, rect.y + std::fmax(0.0f, rect.height));
    return Rect{x0, y0, std::fmax(0.0f, x1 - x0), std::fmax(0.0f, y1 - y0)};
}

std::string parentPath(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return {};
    // Keep the volume root ("C:/") usable as a parent.
    if (slash == 2 && path.size() > 2 && path[1] == ':') return path.substr(0, 3);
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

bool isSeparator(char c) { return c == '/' || c == '\\'; }

// Button width that is at least kMinButtonWidth, never wider than `preferred`,
// and never wider than the space it has to live in.
float buttonWidth(float available, float share, float preferred) {
    return std::fmax(0.0f, std::fmin(available, std::fmin(preferred,
                                                           std::fmax(kMinButtonWidth, available * share))));
}

} // namespace

namespace {

// The two places the theme enums meet, exactly like the language ones in
// applySettings(). core owns settings.ini, ui owns the palette.
theme::ThemeId themeFromTag(ThemeTag tag) {
    switch (tag) {
        case ThemeTag::Light: return theme::ThemeId::Light;
        case ThemeTag::Contrast: return theme::ThemeId::Contrast;
        case ThemeTag::Dark:
        default: return theme::ThemeId::Dark;
    }
}

ThemeTag themeTagFromTheme(theme::ThemeId id) {
    switch (id) {
        case theme::ThemeId::Light: return ThemeTag::Light;
        case theme::ThemeId::Contrast: return ThemeTag::Contrast;
        case theme::ThemeId::Dark:
        default: return ThemeTag::Dark;
    }
}

} // namespace

Launcher::Launcher(i18n::Localizer& localizer) : loc_(localizer) {
    nameField_.id = kNameField;
    rootField_.id = kRootField;
    versionLabel_ = std::string("v") + kEngineVersion;
}

// --- frame ------------------------------------------------------------------

// Qualified: inside a member definition the leading return type is not looked up
// in the class scope.
Launcher::Result Launcher::update(UiRenderer& ui, UiContext& ctx, const Window& window,
                                  int framebufferWidth, int framebufferHeight,
                                  std::string& selectedProjectPath) {
    const int fbWidth = std::max(0, framebufferWidth);
    const int fbHeight = std::max(0, framebufferHeight);
    if (fbWidth <= 0 || fbHeight <= 0) return Result::Stay;

    // The signature takes the window as const, but UiContext::pumpTextField and
    // Window::takeKeyPress mutate it. Dropped once, here, instead of at every
    // call site.
    Window& input = const_cast<Window&>(window);
    ctx.beginInput(input);

    // A confirmation or the folder browser covers the whole window, so while one is
    // up nothing else may answer the pointer. The launcher is one widget layer, so
    // this is the only place that has to say it.
    ctx.setPointerCapture((deleteOpen_ || pickerOpen_) ? kPointerClaim : 0);

    requestedOpen_ = false;
    requestedPath_.clear();
    quitRequested_ = false;

    ui.begin(fbWidth, fbHeight);
    ui.fillRect(0.0f, 0.0f, (float)fbWidth, (float)fbHeight, theme::background);

    recomputeLayout(fbWidth, fbHeight);

    // The launcher owns the whole window: claim the pointer before any widget so
    // no click can reach whatever is behind this state.
    ctx.markHovered(kPointerClaim);

    // Stamped first, so a widget drawn below can tell which frame this is and
    // refuse to act on the very press that opened it.
    ++frame_;

    if (deleteOpen_) {
        // A confirmation is modal: the list behind it is not drawn and not
        // clickable, so a stray click cannot pick a different project between
        // the question and the answer.
        drawTopBar(ui, ctx, fbWidth, fbHeight);
        drawDeleteConfirm(ui, ctx, input, fbWidth, fbHeight);
    } else if (pickerOpen_) {
        drawTopBar(ui, ctx, fbWidth, fbHeight);
        drawFolderPicker(ui, ctx, input, fbWidth, fbHeight);
    } else {
        drawFrame(ui, ctx, input, fbWidth, fbHeight);
        // Keys are resolved after the draw because the draw is what pumps the
        // text fields, and Enter means "commit" or "open" depending on which
        // field owns the focus.
        handleKeys(input);
    }

    ui.end();

    if (requestedOpen_ && openProject(requestedPath_, selectedProjectPath))
        return Result::OpenProject;
    if (quitRequested_) return Result::Quit;
    return Result::Stay;
}

void Launcher::initialize() {
    settings_.load();
    scanIcons();

    rootDraft_ = settings_.projectRoot.empty() ? Settings::defaultProjectRoot()
                                              : settings_.projectRoot;
    // The field buffer is 64 bytes; a longer path is edited through the folder
    // picker instead, which has no length limit.
    rootField_.clear();
    if (rootDraft_.size() < sizeof(rootField_.buffer) - 1) rootField_.set(rootDraft_.c_str());

    applySettings();
}

void Launcher::applySettings() {
    std::string root = str::trim(settings_.projectRoot);
    if (root.empty()) {
        root = Settings::defaultProjectRoot();
        settings_.projectRoot = root;
        rootDraft_ = root;
    }
    projects_.setRoot(root);

    // The one place the two language enums meet. A switch, not a comparison per
    // language: the lists are the same length by hand, and a missing case would
    // show up as a silent fallback to English rather than as a compile error.
    i18n::Language language = i18n::Language::English;
    switch (settings_.language) {
    case LanguageTag::Ukrainian: language = i18n::Language::Ukrainian; break;
    case LanguageTag::German: language = i18n::Language::German; break;
    case LanguageTag::French: language = i18n::Language::French; break;
    case LanguageTag::Italian: language = i18n::Language::Italian; break;
    case LanguageTag::Spanish: language = i18n::Language::Spanish; break;
    case LanguageTag::Lolcat: language = i18n::Language::Lolcat; break;
    case LanguageTag::Jeb: language = i18n::Language::Jeb; break;
    case LanguageTag::Pirate: language = i18n::Language::Pirate; break;
    case LanguageTag::BrokenUtf8: language = i18n::Language::BrokenUtf8; break;
    case LanguageTag::Hex: language = i18n::Language::Hex; break;
    case LanguageTag::GeometryDash: language = i18n::Language::GeometryDash; break;
    case LanguageTag::Portuguese: language = i18n::Language::Portuguese; break;
    case LanguageTag::Minecraft: language = i18n::Language::Minecraft; break;
    case LanguageTag::Undertale: language = i18n::Language::Undertale; break;
    case LanguageTag::Celeste: language = i18n::Language::Celeste; break;
    case LanguageTag::Doom: language = i18n::Language::Doom; break;
    case LanguageTag::WolfOfWallStreet: language = i18n::Language::WolfOfWallStreet; break;
    case LanguageTag::AmongUs: language = i18n::Language::AmongUs; break;
    case LanguageTag::Leet: language = i18n::Language::Leet; break;
    case LanguageTag::Klingon: language = i18n::Language::Klingon; break;
    case LanguageTag::Gopnik: language = i18n::Language::Gopnik; break;
    case LanguageTag::Shakespeare: language = i18n::Language::Shakespeare; break;
    case LanguageTag::Bengali: language = i18n::Language::Bengali; break;
    case LanguageTag::English:
    default: language = i18n::Language::English; break;
    }
    if (loc_.language() != language) loc_.setLanguage(language);

    // ...and the one place the two theme enums meet.
    theme::setActiveTheme(themeFromTag(settings_.theme));

    projects_.scan();
    rebuildVisible();
    clampSelection();
    // Preselect something so Enter always has a target: the most recent project
    // if it is still there, otherwise the first one. Starting with "nothing
    // selected" made the keyboard path a dead end - the user had to press Down
    // before Enter did anything.
    if (selected_ < 0 && !visible_.empty()) {
        selected_ = 0;
        for (const std::string& recent : settings_.recentProjects) {
            for (size_t i = 0; i < visible_.size(); ++i) {
                if (projects_.projects()[(size_t)visible_[i]].path != recent) continue;
                selected_ = (int)i;
                break;
            }
            if (selected_ != 0) break;
        }
    }
    settings_.save();
}

void Launcher::setGraphicsInfo(const char* renderer, const char* version) {
    graphicsRenderer_ = renderer ? renderer : "";
    graphicsVersion_ = version ? version : "";
    diagnosticsDirty_ = true;
}

// --- list model -------------------------------------------------------------

void Launcher::rebuildVisible() {
    const std::vector<ProjectInfo>& all = projects_.projects();
    visible_.clear();
    visible_.reserve(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
        // Favorites is a filter over the same list, so a project keeps one index
        // in one place and the two tabs cannot disagree about what exists.
        if (view_ == View::Favorites && !all[i].favorite) continue;
        visible_.push_back((int)i);
    }
    // A selection that the filter removed must not silently move to a different
    // project: the row under the highlight stays where the user left it.
    listScroll_ = std::clamp(listScroll_, 0, maxListScroll());
}

int Launcher::selectedProject() const {
    if (selected_ < 0 || selected_ >= (int)visible_.size()) return -1;
    return visible_[(size_t)selected_];
}

void Launcher::recomputeLayout(int fbWidth, int fbHeight) {
    const float fw = (float)fbWidth;
    const float fh = (float)fbHeight;

    layout_ = Layout{};
    layout_.topBar = Rect{0.0f, 0.0f, fw, std::fmin(kTopBarHeight, fh)};
    layout_.sidebar = Rect{0.0f, layout_.topBar.height,
                           std::fmin(kSidebarWidth, fw),
                           std::fmax(0.0f, fh - layout_.topBar.height)};
    const float footerH = std::fmin(kFooterHeight, std::fmax(0.0f, fh * 0.1f));
    layout_.footer = Rect{layout_.sidebar.width, fh - footerH,
                          std::fmax(0.0f, fw - layout_.sidebar.width), footerH};

    const float contentX = layout_.sidebar.width;
    layout_.content = Rect{contentX, layout_.topBar.height,
                           std::fmax(0.0f, fw - contentX),
                           std::fmax(0.0f, layout_.footer.y - layout_.topBar.height)};

    layout_.newProject = layout_.content;
    layout_.settings = layout_.content;
    layout_.account = layout_.content;
}

int Launcher::listRows() const {
    const float rowsH = layout_.content.height - kListOverhead;
    return rowsH > 0.0f ? (int)(rowsH / kRowHeight) : 0;
}

int Launcher::maxListScroll() const {
    return std::max(0, (int)visible_.size() - listRows());
}

// --- frame drawing ----------------------------------------------------------

void Launcher::drawFrame(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth,
                         int fbHeight) {
    ui.fillRect(layout_.sidebar.x, layout_.sidebar.y, layout_.sidebar.width,
                layout_.sidebar.height, theme::panel);
    drawTopBar(ui, ctx, fbWidth, fbHeight);
    drawSidebar(ui, ctx, fbWidth, fbHeight);

    switch (view_) {
        case View::NewProject:
            drawNewProject(ui, ctx, window, fbWidth);
            break;
        case View::Settings:
            drawSettingsPanel(ui, ctx, window, fbWidth);
            break;
        case View::Account:
            drawAccount(ui, ctx, fbWidth);
            break;
        case View::Projects:
        case View::Favorites:
        default:
            drawProjectList(ui, ctx, fbWidth);
            break;
    }
    drawFooter(ui, ctx, fbWidth);
}

void Launcher::drawTopBar(UiRenderer& ui, UiContext& ctx, int fbWidth, int fbHeight) {
    const Rect& bar = layout_.topBar;
    if (bar.height <= 0.0f) return;

    ui.fillRect(bar.x, bar.y, bar.width, bar.height, theme::header);
    ui.fillRect(bar.x, bar.y + bar.height - 1.0f, bar.width, 1.0f, theme::separator);
    textClipRight_ = -1.0f;

    const float midY = bar.y + (bar.height - kTextHeight) * 0.5f;
    const float brandX = bar.x + kPadding;

    // Brand, then the tabs, then the count and New Project on the right.
    drawText(ui, brandX, midY - 6.0f, kEngineName, theme::text, fbWidth);
    drawText(ui, brandX, midY + 9.0f, versionLabel_.c_str(), theme::accentDim, fbWidth);

    float tabX = brandX + 150.0f;
    const float tabY = bar.y + (bar.height - kNavHeight) * 0.5f;
    const bool onProjects = view_ == View::Projects || view_ == View::NewProject;
    const float allW = 58.0f, favW = 84.0f;

    if (ctx.button(ui, kTabAll, tabX, tabY, allW, kNavHeight, text("launcher.tabAll"), onProjects))
        setView(View::Projects);
    const float favX = tabX + allW + 4.0f;
    if (ctx.button(ui, kTabFavorites, favX, tabY, favW, kNavHeight, text("launcher.tabFavorites"),
                   view_ == View::Favorites))
        setView(View::Favorites);

    // Right side: the count of what is on screen, then the primary action.
    char countLabel[64];
    std::snprintf(countLabel, sizeof(countLabel), text("launcher.projectCount"),
                  (int)visible_.size());
    const float newW = buttonWidth(bar.width * 0.5f, 0.3f, 120.0f);
    const Rect newButton{bar.x + bar.width - newW - kPadding,
                         bar.y + (bar.height - kRowHeight * 0.5f) * 0.5f, newW, kRowHeight * 0.5f};
    if (ctx.button(ui, kNewProjectButton, newButton.x, newButton.y, newButton.width,
                   newButton.height, text("launcher.newProject")))
        setView(View::NewProject);

    drawTextRight(ui, newButton.x - 10.0f, midY, countLabel, theme::textDim, fbWidth);

    // The GL line only fits when there is spare width; it is diagnostics, not
    // navigation, so it is the first thing to go on a narrow window.
    if (diagnosticsDirty_ || diagnosticsWidth_ != fbWidth || diagnosticsHeight_ != fbHeight) {
        diagnosticsDirty_ = false;
        diagnosticsWidth_ = fbWidth;
        diagnosticsHeight_ = fbHeight;
        char buffer[512];
        const char* renderer = graphicsRenderer_.empty() ? nullptr : graphicsRenderer_.c_str();
        const char* version = graphicsVersion_.empty() ? nullptr : graphicsVersion_.c_str();
        if (renderer && version)
            std::snprintf(buffer, sizeof(buffer), "OpenGL %s | %s | %dx%d", version, renderer,
                          fbWidth, fbHeight);
        else if (renderer)
            std::snprintf(buffer, sizeof(buffer), "%s | %dx%d", renderer, fbWidth, fbHeight);
        else
            std::snprintf(buffer, sizeof(buffer), "%dx%d framebuffer", fbWidth, fbHeight);
        diagnostics_.assign(buffer);
    }
    const float diagnosticsRight = favX + favW + 16.0f;
    if (diagnosticsRight < bar.x + bar.width - newButton.width - 200.0f)
        drawText(ui, diagnosticsRight, midY, diagnostics_.c_str(), theme::textDim, fbWidth);
}

void Launcher::drawSidebar(UiRenderer& ui, UiContext& ctx, int fbWidth, int fbHeight) {
    const Rect& side = layout_.sidebar;
    if (side.width <= 0.0f) return;
    textClipRight_ = side.x + side.width - 6.0f;

    // Top: the one destination that has a list behind it.
    const Rect projects{side.x + 6.0f, side.y + 10.0f, std::fmax(0.0f, side.width - 12.0f),
                        kNavHeight};
    const bool onProjects = view_ == View::Projects || view_ == View::Favorites;
    if (ctx.button(ui, kSidebarProjects, projects.x, projects.y, projects.width, projects.height,
                   text("launcher.navProjects"), onProjects))
        setView(View::Projects);

    // Bottom: account on the left, the gear that opens settings on the right.
    const float bottomY = side.y + side.height - kNavHeight - 10.0f;
    const float gearSize = kNavHeight;
    const Rect account{side.x + 6.0f, bottomY, std::fmax(0.0f, side.width - 12.0f - gearSize - 6.0f),
                       kNavHeight};
    if (ctx.button(ui, kSidebarAccount, account.x, account.y, account.width, account.height,
                   text("launcher.navAccount"), view_ == View::Account))
        setView(View::Account);

    const Rect gear{side.x + side.width - gearSize - 6.0f, bottomY, gearSize, kNavHeight};
    const bool gearHovered = ctx.hovered(gear);
    if (ctx.button(ui, kSidebarSettings, gear.x, gear.y, gear.width, gear.height, "",
                   view_ == View::Settings))
        setView(View::Settings);
    drawGear(ui, gear, view_ == View::Settings, gearHovered);

    // The root the list is showing, so an empty list is explainable. Shrunk to
    // fit the sidebar rather than cut: which folder this is, is the whole point.
    const float hintY = bottomY - kTextHeight - 8.0f;
    if (hintY > side.y + 44.0f)
        drawTextFit(ui, side.x + 9.0f, hintY, side.width - 18.0f, /*keepTail=*/true,
                    projects_.root(), theme::textDim, fbWidth);
}

void Launcher::drawProjectList(UiRenderer& ui, UiContext& ctx, int fbWidth) {
    const Rect& area = layout_.content;
    if (area.width <= 0.0f) return;
    textClipRight_ = area.x + area.width - 6.0f;

    char countLabel[64];
    std::snprintf(countLabel, sizeof(countLabel), text("launcher.projectCount"),
                  (int)visible_.size());
    float y = 0.0f;
    const Rect body = section(ui, ctx, area, sectionTitle(), countLabel, fbWidth, y);
    if (body.width <= 0.0f) return;

    if (visible_.empty()) {
        // The two empty states differ: "no projects at all" is a creation
        // problem, "no favorites" is a filter the user set themselves.
        if (view_ == View::Favorites)
            drawEmpty(ui, fbWidth, text("launcher.noFavorites"), text("launcher.noFavoritesHint"));
        else
            drawEmpty(ui, fbWidth, text("launcher.emptyTitle"), text("launcher.emptyHint"));
        return;
    }

    const float rowsTop = y;
    const int visibleCount = listRows();
    if (visibleCount <= 0) return;

    // Wheel scrolls the list, but only while the pointer is over it.
    if (ctx.scrollDelta() != 0.0 &&
        ctx.hovered(Rect{body.x, rowsTop, body.width, (float)visibleCount * kRowHeight})) {
        const int delta = (int)ctx.scrollDelta();
        listScroll_ = std::clamp(listScroll_ - delta, 0, std::max(0, (int)visible_.size() - visibleCount));
    }
    listScroll_ = std::clamp(listScroll_, 0, maxListScroll());

    for (int row = listScroll_; row < (int)visible_.size() && row < listScroll_ + visibleCount;
         ++row) {
        const ProjectInfo& info = projects_.projects()[(size_t)visible_[(size_t)row]];
        const Rect line{body.x, rowsTop + (float)(row - listScroll_) * kRowHeight, body.width,
                        kRowHeight - 2.0f};

        const int base = kRowIdBase + row * kRowIdStride;
        const bool selected = row == selected_;
        const bool hovered = ctx.hovered(line);
        const int rowId = base + kRowSlotRow;

        // Row background, drawn before the widgets so the buttons sit on top of
        // it and the hover highlight reads as one surface.
        const Color rowBackground =
            selected ? theme::accentDim : (hovered ? theme::buttonHover : theme::panel);
        ui.fillRect(line.x, line.y, line.width, line.height, rowBackground);
        if (selected) ui.fillRect(line.x, line.y, 2.0f, line.height, theme::accent);
        if (hovered) ctx.markHovered(rowId);

        // Buttons are laid out from the right edge inwards, in the order the
        // concept sketch shows them: Open, then Delete.
        const float deleteW = kDeleteButtonWidth;
        const float openW = kButtonWidth;
        const Rect deleteButton{line.x + line.width - deleteW - 8.0f, line.y + (line.height - kRowHeight * 0.5f) * 0.5f,
                                deleteW, kRowHeight * 0.5f};
        const Rect openButton{deleteButton.x - openW - 6.0f, deleteButton.y, openW,
                              deleteButton.height};
        const Rect star{line.x + 8.0f, line.y + (line.height - kStarSize) * 0.5f, kStarSize,
                        kStarSize};
        const Rect icon{star.x + kStarSize + 8.0f, line.y + (line.height - kRowIcon) * 0.5f,
                        kRowIcon, kRowIcon};

        drawProjectIcon(ui, icon, selected, info.favorite);
        drawStar(ui, ctx, base + kRowSlotFavorite, star, info.favorite, rowBackground);
        if (ctx.button(ui, base + kRowSlotDelete, deleteButton.x, deleteButton.y,
                       deleteButton.width, deleteButton.height, text("launcher.delete")))
            askDelete(row);
        if (ctx.button(ui, base + kRowSlotOpen, openButton.x, openButton.y, openButton.width,
                       openButton.height, text("launcher.open")))
            requestOpen(info.path);

        // Name over path, both shrunk into the space between the icon and the
        // buttons so a deep path stays readable instead of becoming "...".
        const float textLeft = icon.x + kRowIcon + 10.0f;
        const float textRight = openButton.x - 10.0f;
        const float slot = textRight - textLeft;
        if (slot > 40.0f) {
            drawTextFit(ui, textLeft, line.y + 7.0f, slot, /*keepTail=*/false, info.name,
                        theme::text, fbWidth);
            drawTextFit(ui, textLeft, line.y + 7.0f + kTextHeight + 2.0f, slot, /*keepTail=*/true,
                        info.path, theme::textDim, fbWidth);
        }

        // Clicking the row itself selects it; Enter then opens it. The buttons
        // already consumed their own clicks, so this only fires on the empty part.
        if (hovered && ctx.pointerPressed() && ctx.hoveredId() == rowId) selected_ = row;
    }
}

void Launcher::drawEmpty(UiRenderer& ui, int fbWidth, const char* key, const char* hint) {
    const Rect body = layout_.content;
    textClipRight_ = body.x + body.width - 6.0f;
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), key, projects_.root().c_str());
    drawText(ui, body.x + kPadding, body.y + 40.0f, buffer, theme::textDim, fbWidth);
    if (hint && *hint) {
        std::snprintf(buffer, sizeof(buffer), hint, projects_.root().c_str());
        drawText(ui, body.x + kPadding, body.y + 40.0f + kTextHeight + 6.0f, buffer,
                 theme::textDim, fbWidth);
    }
}

void Launcher::drawNewProject(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth) {
    const Rect& area = layout_.content;
    if (area.width <= 0.0f) return;
    textClipRight_ = area.x + area.width - 6.0f;

    const float panelW = std::fmin(520.0f, area.width - 2.0f * kPadding);
    const float panelH = std::fmin(216.0f, area.height - 2.0f * kPadding);
    const Rect panel = intersect(Rect{area.x + (area.width - panelW) * 0.5f,
                                      area.y + (area.height - panelH) * 0.5f, panelW, panelH},
                                 area.x + area.width, area.y + area.height);
    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::accent);

    float y = panel.y + 14.0f;
    drawText(ui, panel.x + kPadding, y, text("launcher.newProject"), theme::text, fbWidth);
    y += kTextHeight + 12.0f;

    const float innerW = std::fmax(0.0f, panel.width - 2.0f * kPadding);
    const float buttonW = buttonWidth(innerW, 0.35f, kButtonWidth);

    // Name.
    const Rect field{panel.x + kPadding, y, std::fmax(0.0f, innerW - buttonW - 6.0f),
                     kFieldHeight};
    const Rect create{field.x + field.width + 6.0f, y, buttonW, kFieldHeight};
    ctx.pumpTextField(window, nameField_, ctx.hovered(field));
    drawField(ui, field, nameField_, text("launcher.namePlaceholder"), fbWidth);

    const bool canCreate = ProjectLibrary::isValidName(str::trim(nameField_.text()));
    if (canCreate) {
        if (ctx.button(ui, kCreateButton, create.x, create.y, create.width, create.height,
                       text("launcher.create")))
            createProject();
    } else {
        drawDisabledButton(ui, ctx, kCreateButton, create, text("launcher.create"), fbWidth);
    }

    drawText(ui, panel.x + kPadding, create.y + create.height + 6.0f, nameHint(),
             canCreate ? theme::textDim : theme::warning, fbWidth);

    // Where the project will be created. This is the one thing a user cannot
    // guess - the default is a `project` folder next to the binary - so it is
    // editable right here instead of being hidden behind the settings gear.
    y = create.y + create.height + 6.0f + kTextHeight + 14.0f;
    ui.fillRect(panel.x + kPadding, y - 6.0f, innerW, 1.0f, theme::separator);
    drawText(ui, panel.x + kPadding, y, text("settings.projectRoot"), theme::textDim, fbWidth);

    const float rootY = y + kTextHeight + 4.0f;
    const float browseW = buttonWidth(innerW, 0.32f, kButtonWidth);
    const Rect browse{panel.x + panel.width - browseW - kPadding, rootY, browseW, kFieldHeight};
    const Rect rootField{panel.x + kPadding, rootY, std::fmax(0.0f, innerW - browseW - 6.0f),
                         kFieldHeight};
    ctx.pumpTextField(window, rootField_, ctx.hovered(rootField));
    if (rootField_.editing) rootDraft_.assign(rootField_.text());
    drawFieldValue(ui, rootField, rootDraft_.c_str(), rootField_.editing, /*asPath=*/true, fbWidth);
    if (ctx.button(ui, kBrowseButton, browse.x, browse.y, browse.width, browse.height,
                   text("settings.browse")))
        openPicker();
    // Apply is only offered while the typed root differs from the one in use, so
    // Create never silently lands in a different folder than the one shown.
    if (rootDraft_ != settings_.projectRoot) {
        const float applyW = buttonWidth(innerW, 0.3f, kButtonWidth);
        const Rect apply{panel.x + kPadding, rootY + kFieldHeight + 6.0f, applyW, kRowHeight};
        drawText(ui, apply.x, apply.y + 2.0f, text("settings.rootChanged"), theme::warning, fbWidth);
        const Rect applyButton{panel.x + panel.width - applyW - kPadding, apply.y, applyW,
                               apply.height};
        if (ctx.button(ui, kApplyButton, applyButton.x, applyButton.y, applyButton.width,
                       applyButton.height, text("settings.apply")))
            applyRoot(rootDraft_);
    }

    const Rect back{panel.x + kPadding, panel.y + panel.height - kFieldHeight - 12.0f,
                    buttonW, kFieldHeight};
    if (ctx.button(ui, kCancelCreateButton, back.x, back.y, back.width, back.height,
                   text("launcher.cancel")))
        setView(View::Projects);
}

// The closed half of a dropdown: caption, current value, the arrow, and the hit
// test that opens it. Drawn among the other rows; the list comes later.
Launcher::SelectPaint Launcher::drawSelectField(UiRenderer& ui, UiContext& ctx, const Rect& rect,
                                               const char* caption, const SelectItem* items,
                                               int count, int current, int id, int fbWidth,
                                               int columns, const char* const* columnTitles) {
    textClipRight_ = rect.x + rect.width;
    const Rect field{rect.x, rect.y + kTextHeight + 4.0f, rect.width, kFieldHeight + 6.0f};

    SelectPaint paint;
    paint.field = field;
    paint.items = items;
    paint.count = count;
    paint.current = current;
    paint.columns = std::clamp(columns, 1, kMaxColumns);
    for (int i = 0; i < paint.columns; ++i)
        paint.columnTitles[i] = columnTitles ? columnTitles[i] : nullptr;
    if (field.width <= 0.0f || field.height <= 0.0f) return paint;
    paint.id = id;

    if (caption) drawText(ui, field.x, rect.y, caption, theme::textDim, fbWidth);

    const char* currentLabel = "";
    for (int i = 0; i < count; ++i)
        if (items[i].value == current) currentLabel = items[i].label;

    // Hit area is the field itself; the background is drawn here rather than
    // through UiContext::button so the open state can look like a field and not
    // like a button.
    const bool fieldHovered = ctx.hovered(field) && (openSelect_ < 0 || openSelect_ == id);
    if (fieldHovered) ctx.markHovered(id);
    if (fieldHovered && ctx.pointerPressed()) {
        const bool opening = openSelect_ != id;
        openSelect_ = opening ? id : -1;
        // A fresh list starts at the top, except when reopening the one it was
        // left on - flipping back and forth while choosing should not lose the
        // place. Scrolling to the current value instead means "Minecraft" is
        // visible the moment twenty-three languages open.
        if (opening) {
            // The row of the current value - in its own column. A list with two
            // columns has a row number in each of them, and the value is found
            // by its column first and by its row after that.
            int currentColumn = 0;
            for (int i = 0; i < count; ++i)
                if (items[i].value == current) {
                    currentColumn = std::clamp(items[i].column, 0, paint.columns - 1);
                    break;
                }
            int currentRow = 0;
            for (int i = 0; i < count; ++i) {
                if (std::clamp(items[i].column, 0, paint.columns - 1) != currentColumn) continue;
                if (items[i].value == current) break;
                ++currentRow;
            }
            selectScroll_ = currentRow;
            selectHoverRow_ = -1;
            selectOpenedFrame_ = frame_;
        }
    }

    ui.fillRect(field.x, field.y, field.width, field.height,
                openSelect_ == id ? theme::accentDim : theme::field);
    drawText(ui, field.x + 6.0f, field.y + (field.height - kTextHeight) * 0.5f, currentLabel,
             theme::text, fbWidth);

    // A downward triangle drawn from three rows. The atlas may not have a glyph for
    // it, and a shape cannot go missing.
    const float arrowX = field.x + field.width - 12.0f;
    const float arrowY = field.y + field.height * 0.5f - 1.0f;
    for (int row = 0; row < 4; ++row) {
        const float inset = (float)(3 - row) * 0.9f;
        ui.fillRect(arrowX + 0.9f - inset, arrowY + (float)row * 1.2f, 1.8f + inset * 2.0f, 1.2f,
                    openSelect_ == id ? theme::text : theme::textDim);
    }
    return paint;
}

// The open half, drawn after every row of the panel and after a flush: the batcher
// emits every rectangle before any text, so without the flush the list's own
// background would land under the labels drawn before it.
int Launcher::drawSelectList(UiRenderer& ui, UiContext& ctx, const SelectPaint& paint,
                             const Rect& bounds, int fbWidth) {
    if (paint.id < 0 || openSelect_ != paint.id || paint.count <= 0) return -1;

    const int columns = std::clamp(paint.columns, 1, kMaxColumns);
    // The gutter on the right is where the scrollbar goes, so the columns do
    // not grow over it.
    const float gutter = 6.0f;
    const float gap = columns > 1 ? 10.0f : 0.0f;
    const float columnWidth = std::fmax(
        40.0f, (paint.field.width - gutter - gap * (float)(columns - 1)) / (float)columns);
    // The text of a row has the tick in the gutter and a little air beside it.
    const float textRoom = columnWidth - 18.0f;

    // THE ITEMS OF EACH COLUMN.
    //
    // A list of languages has two kinds of entry in it: the languages an
    // interface is written in, and the meme languages - the ones that are there
    // to be funny. They are different things, so they go in different columns.
    // One list of twenty-three rows that has to be scrolled to find the language
    // of the interface is a list that hides what it is for.
    std::vector<int> order[kMaxColumns];
    for (int i = 0; i < paint.count; ++i) {
        const int c = std::clamp(paint.items[i].column, 0, columns - 1);
        order[c].push_back(i);
    }

    // THE LINES OF EVERY ITEM.
    //
    // A name that does not fit its column is broken into two lines instead of
    // being cut off: a cut off name is a name nobody can read, and a name
    // shrunk until it fits is a name in letters too small to read either.
    std::vector<std::vector<std::string>> lines((size_t)paint.count);
    std::vector<float> heights((size_t)paint.count, 0.0f);
    for (int i = 0; i < paint.count; ++i) {
        lines[(size_t)i] = ui.wrapText(paint.items[i].label, textRoom);
        if (lines[(size_t)i].empty()) lines[(size_t)i].push_back(paint.items[i].label);
        heights[(size_t)i] = (float)lines[(size_t)i].size() * (float)kTextHeight + 4.0f;
    }

    // Where every row of every column starts, and how tall the columns are.
    std::vector<float> tops[kMaxColumns];
    float contentHeight = 0.0f;
    for (int c = 0; c < columns; ++c) {
        float top = 0.0f;
        for (int index : order[c]) {
            tops[c].push_back(top);
            top += heights[(size_t)index];
        }
        contentHeight = std::fmax(contentHeight, top);
    }

    // The caption of the columns, when the list has any.
    float headerH = 0.0f;
    for (int c = 0; c < columns; ++c)
        if (paint.columnTitles[c]) headerH = (float)kTextHeight + 4.0f;

    const float fullHeight = headerH + contentHeight;
    const float below = paint.field.y + paint.field.height + 2.0f;
    const float above = paint.field.y - 2.0f - fullHeight;
    // Down when it fits, up when it does not. Six languages on a short panel do fit
    // below; a long list near the bottom of the window does not, and one that runs
    // off the screen edge is a list you cannot scroll to.
    //
    // The clamp is what stops it covering the field it belongs to. With the list
    // taller than the panel, `below + fullHeight` overshoots and `above` is off
    // the top, so the old clamp pinned y to the panel's top edge - which is where
    // the field itself sits. The list then landed on top of its own field, the
    // pointer was already inside a row, and the click that opened the dropdown
    // also chose a language. Pinning to the space on one side of the field keeps
    // the two disjoint.
    float y = below;
    if (below + fullHeight > bounds.y + bounds.height && above >= bounds.y) y = above;
    y = clampf(y, bounds.y, std::fmax(bounds.y, bounds.y + bounds.height - fullHeight));
    // Whatever the height ends up being, the list stays strictly on one side of
    // the field: never over it, or the field becomes impossible to point at again.
    if (y + fullHeight > paint.field.y) y = std::fmin(below, paint.field.y);
    y = std::fmax(y, bounds.y);

    // Twenty-three languages are taller than the settings panel and usually taller
    // than the viewport, so the list is clamped to what is reachable on screen and
    // scrolled. Without this the last entries are painted below the window edge:
    // visible in the framebuffer, impossible to click.
    const float roomBelow = bounds.y + bounds.height - std::fmax(y, below);
    const float roomAbove = paint.field.y - 2.0f - bounds.y;
    // Whichever side it opened towards, it may not grow past the field or past the
    // panel; a list clipped by its own scroll offset reads as a short list.
    const float room = std::max(0.0f, y < paint.field.y ? roomAbove : roomBelow);
    const float maxHeight = std::fmax((float)kRowHeight, std::min(room, bounds.height));

    // How many rows of each column are on screen. Every column is scrolled by
    // the same number of rows, so the two of them are read together.
    int visibleRows[kMaxColumns] = {0, 0};
    float usedHeight = 0.0f;
    for (int c = 0; c < columns; ++c) {
        const int count = (int)order[c].size();
        const int start = std::min(selectScroll_, std::max(0, count - 1));
        float used = 0.0f;
        int shown = 0;
        for (int r = start; r < count; ++r) {
            const int index = order[c][(size_t)r];
            if (shown > 0 && used + heights[(size_t)index] > maxHeight - headerH) break;
            used += heights[(size_t)index];
            ++shown;
        }
        visibleRows[c] = shown;
        usedHeight = std::fmax(usedHeight, used);
    }
    const int shownRows = std::max(1, std::max(visibleRows[0], visibleRows[1]));
    int maxScroll = 0;
    for (int c = 0; c < columns; ++c)
        maxScroll = std::max(maxScroll, (int)order[c].size() - visibleRows[c]);

    // Opening on the current value would put a language near the end of a
    // twenty-three row list below the fold with everything after it. Start one
    // screen higher, so the choice and its neighbours are all on screen: the
    // point of opening the list is to see what else there is.
    if (frame_ == selectOpenedFrame_)
        selectScroll_ = std::clamp(selectScroll_ - shownRows / 2, 0, maxScroll);
    else
        selectScroll_ = std::clamp(selectScroll_, 0, maxScroll);
    // Published for the key handler, which runs outside this draw and would
    // otherwise be paging a list whose height it cannot know.
    openSelectRows_ = shownRows;
    openSelectScrollMax_ = maxScroll;

    const float height = headerH + usedHeight;
    const Rect list{paint.field.x, y, paint.field.width, height};
    ui.fillRect(list.x, list.y, list.width, list.height, theme::panel);

    // The text of a column stops at the edge of the column: a name that runs
    // into the column next to it is a name nobody can read.
    const float clipBefore = textClipRight_;

    // The captions of the columns.
    for (int c = 0; c < columns; ++c) {
        if (!paint.columnTitles[c]) continue;
        const float x = paint.field.x + (float)c * (columnWidth + gap);
        textClipRight_ = std::fmin(clipBefore, x + columnWidth - 2.0f);
        drawText(ui, x + 18.0f, list.y + 2.0f, paint.columnTitles[c], theme::textDim, fbWidth);
    }
    // A line between the columns, so that the two of them are two.
    if (columns > 1) {
        const float x = paint.field.x + columnWidth + gap * 0.5f;
        ui.fillRect(x, list.y + headerH, 1.0f, list.height - headerH, theme::field);
    }

    // Swallow the click while the pointer is over the list. The list is painted
    // last, after every row of the panel has already tested this frame's press and
    // release - so a click on "Minecraft" also reached the Rescan button drawn
    // underneath it and fired that instead. Consuming the event here is what makes
    // the dropdown pick a language instead of pressing the button behind it.
    // The click that opened this list is still `pointerPressed` this frame: the
    // field is drawn among the panel's rows and the list right after it, both
    // before the frame ends. Treating that same press as a choice is why picking a
    // language needed no second click - the pointer never had to enter the list,
    // so the row under it was chosen for the user. A list needs a frame to exist
    // before it can be pointed at.
    const bool justOpened = frame_ == selectOpenedFrame_;
    const bool overList = ctx.hovered(list);

    int chosen = -1;
    for (int c = 0; c < columns; ++c) {
        const int count = (int)order[c].size();
        if (count == 0) continue;
        const float x = paint.field.x + (float)c * (columnWidth + gap);
        textClipRight_ = std::fmin(clipBefore, x + columnWidth - 2.0f);
        const int start = std::min(selectScroll_, std::max(0, count - 1));
        int shown = 0;
        for (int r = start; r < count && shown < visibleRows[c]; ++r) {
            const int item = order[c][(size_t)r];
            const float rowY = list.y + headerH + tops[c][(size_t)r] - tops[c][(size_t)start];
            const Rect row{x, rowY, columnWidth, heights[(size_t)item]};
            const bool hovered = ctx.hovered(row);
            const bool selected = paint.items[item].value == paint.current;
            if (hovered) {
                ui.fillRect(row.x, row.y, row.width, row.height, theme::buttonHover);
                ctx.markHovered(paint.id + 100 + item);
                if (!justOpened) {
                    selectHoverRow_ = r;
                    selectHoverColumn_ = c;
                }
            }
            // The selection is stated three ways, because one is not enough to read at
            // a glance: the accent bar, the accent text, and a tick in the gutter. The
            // bar alone is easy to miss on a row that is also hovered.
            const Color color = selected ? theme::accent : theme::text;
            // The lines of the name, one under the other. A name that did not fit
            // its column is two lines now, and both of them are read.
            float lineY = row.y + 2.0f;
            for (const std::string& line : lines[(size_t)item]) {
                drawText(ui, row.x + 18.0f, lineY, line.c_str(), color, fbWidth);
                lineY += (float)kTextHeight;
            }
            if (selected) {
                ui.fillRect(row.x + 2.0f, row.y, 2.0f, row.height, theme::accent);
                // The tick is drawn from rows, not printed. U+2713 is outside the
                // atlas - the alphabet stops at Latin-1 plus a handful of extras - so
                // printing it yields a blank box, which is worse than no tick.
                const float tickX = row.x + 7.0f;
                const float tickY = row.y + row.height * 0.5f;
                for (int s = 0; s < 4; ++s) {
                    const float t = (float)s / 3.0f;
                    // Short arm down-right, long arm up-right: a check without a glyph.
                    const float ax = tickX + (t * 3.0f);
                    const float ay = tickY + (2.0f - t * 2.0f);
                    ui.fillRect(ax, ay, 1.4f, 1.4f, theme::accent);
                    const float bx = tickX + 2.6f + t * 4.4f;
                    const float by = tickY + (1.0f - t * 4.4f);
                    ui.fillRect(bx, by, 1.4f, 1.4f, theme::accent);
                }
            }

            if (hovered && ctx.pointerPressed() && !justOpened) {
                openSelect_ = -1;
                selectHoverRow_ = -1;
                chosen = paint.items[item].value;
                ctx.consumePointerClick();
            }
            ++shown;
        }
    }
    textClipRight_ = clipBefore;

    // A scrollbar, only when there is more to scroll to. Drawn as a plain trough so
    // it cannot be mistaken for a draggable widget: the list scrolls with the wheel
    // and with Up/Down, which is enough for a list this size.
    if (maxScroll > 0) {
        const float trackX = list.x + list.width - 4.0f;
        ui.fillRect(trackX, list.y, 2.0f, list.height, theme::field);
        const float thumb =
            std::fmax(18.0f, list.height * (usedHeight / std::fmax(1.0f, contentHeight)));
        const float span = list.height - thumb;
        const float thumbY = list.y + span * ((float)selectScroll_ / (float)maxScroll);
        ui.fillRect(trackX, thumbY, 2.0f, thumb, theme::accent);
    }

    // Scrolling: the wheel over the list, and Up/Down while it is open, so the
    // list can be walked without hunting for a scrollbar.
    //
    // The wheel is counted in notches - one up, one down - and a notch is a
    // row. Dividing the number by forty and rounding it up made half of the
    // notches disappear: a notch down was a quarter of a row, rounded up to
    // nothing, and the list would only go up.
    if (overList && ctx.scrollDelta() != 0.0f) {
        const int notches = (int)ctx.scrollDelta();
        selectScroll_ = std::clamp(selectScroll_ - notches, 0, maxScroll);
    }
    // Keyboard walk: Down/Up page through the list, Home/End jump to the ends.
    // A twenty-three entry list is exactly the case where reaching an entry with
    // the mouse is not the obvious thing to do.
    if (ctx.pointerDown() && selectHoverRow_ >= 0) {
        // Keep the row under the pointer in view, so the list can be walked with
        // the mouse held down rather than requiring a fresh click on every row.
        const int rows = visibleRows[std::clamp(selectHoverColumn_, 0, columns - 1)];
        if (selectHoverRow_ < selectScroll_) selectScroll_ = selectHoverRow_;
        else if (selectHoverRow_ >= selectScroll_ + rows)
            selectScroll_ = selectHoverRow_ - rows + 1;
        selectScroll_ = std::clamp(selectScroll_, 0, maxScroll);
    }

    // A click outside the list closes it without choosing anything - but not on the
    // frame it opened, where that same click is the one that opened it.
    if (ctx.pointerPressed() && !justOpened && !overList && !ctx.hovered(paint.field)) {
        openSelect_ = -1;
        ctx.consumePointerClick();
    }
    return chosen;
}

void Launcher::drawSettingsPanel(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth) {
    const Rect& area = layout_.content;
    if (area.width <= 0.0f) return;
    textClipRight_ = area.x + area.width - 6.0f;

    float y = 0.0f;
    const Rect body = section(ui, ctx, area, text("launcher.settings"), nullptr, fbWidth, y);
    if (body.width <= 0.0f || body.height <= 0.0f) return;
    y = body.y;

    // While a dropdown is open it floats over the rows below it, but those rows
    // have already run their own hit tests earlier in this frame - so a click on
    // "Minecraft" also pressed the Rescan button sitting underneath it. Suppressing
    // the panel's controls while a list is open makes the dropdown the only thing
    // under the pointer, which is what a visible list should be.
    const bool listOpen = openSelect_ >= 0;

    // Row 1: label + root path field + Browse.
    drawText(ui, body.x, y, text("settings.projectRoot"), theme::textDim, fbWidth);
    const float fieldY = y + kTextHeight + 2.0f;
    const float browseW = buttonWidth(body.width, 0.32f, kButtonWidth);
    const Rect browse{body.x + body.width - browseW, fieldY, browseW, kFieldHeight};
    const Rect rootField{body.x, fieldY, std::fmax(0.0f, body.width - browseW - 6.0f), kFieldHeight};

    ctx.pumpTextField(window, rootField_, ctx.hovered(rootField) && !listOpen);
    if (rootField_.editing) rootDraft_.assign(rootField_.text());
    drawFieldValue(ui, rootField, rootDraft_.c_str(), rootField_.editing, /*asPath=*/true,
                   fbWidth);

    if (!listOpen && ctx.button(ui, kBrowseButton, browse.x, browse.y, browse.width,
                                browse.height, text("settings.browse")))
        openPicker();

    // Row 2: Apply + Reset to default. Both sized to their labels, so the pair does
    // not stretch into a bar that looks like a third field.
    float rowY = fieldY + kFieldHeight + 6.0f;
    const float applyW = buttonWidthFor(ui, text("settings.apply"), kMinButtonWidth);
    const float resetW = buttonWidthFor(ui, text("settings.reset"), kMinButtonWidth);
    const Rect apply{body.x, rowY, applyW, kRowHeight};
    const Rect reset{apply.x + apply.width + 6.0f, rowY, resetW, kRowHeight};

    // Apply is only meaningful when the field says something new.
    if (rootDraft_ != settings_.projectRoot) {
        if (!listOpen && ctx.button(ui, kApplyButton, apply.x, apply.y, apply.width,
                                    apply.height, text("settings.apply")))
            applyRoot(rootDraft_);
    } else {
        drawDisabledButton(ui, ctx, kApplyButton, apply, text("settings.apply"), fbWidth);
    }
    if (!listOpen && ctx.button(ui, kResetButton, reset.x, reset.y, reset.width,
                                reset.height, text("settings.reset")))
        applyRoot(Settings::defaultProjectRoot());

    // Row 3: language. A list, like the theme below it, because there are six of
    // them now and a row of buttons would have to shrink the names to nothing.
    //
    // The list has two columns. The languages an interface is written in are on
    // one side, and the meme languages - the ones that are there to be funny -
    // on the other. They are different things, and a list of twenty-three rows
    // that has to be scrolled to find the language of the interface hides what
    // it is for.
    rowY += kRowHeight + 6.0f;
    const SelectItem languages[] = {
        {text("settings.languageEnglish"), (int)LanguageTag::English},
        {text("settings.languageUkrainian"), (int)LanguageTag::Ukrainian},
        {text("settings.languageGerman"), (int)LanguageTag::German},
        {text("settings.languageFrench"), (int)LanguageTag::French},
        {text("settings.languageItalian"), (int)LanguageTag::Italian},
        {text("settings.languageSpanish"), (int)LanguageTag::Spanish},
        {text("settings.languagePortuguese"), (int)LanguageTag::Portuguese},
        {text("settings.languageBengali"), (int)LanguageTag::Bengali},
        // The meme languages, in the second column.
        {text("settings.languageLolcat"), (int)LanguageTag::Lolcat, nullptr, 1},
        {text("settings.languageJeb"), (int)LanguageTag::Jeb, nullptr, 1},
        {text("settings.languagePirate"), (int)LanguageTag::Pirate, nullptr, 1},
        {text("settings.languageBrokenUtf8"), (int)LanguageTag::BrokenUtf8, nullptr, 1},
        {text("settings.languageHex"), (int)LanguageTag::Hex, nullptr, 1},
        {text("settings.languageGD"), (int)LanguageTag::GeometryDash, nullptr, 1},
        {text("settings.languageMinecraft"), (int)LanguageTag::Minecraft, nullptr, 1},
        {text("settings.languageUndertale"), (int)LanguageTag::Undertale, nullptr, 1},
        {text("settings.languageCeleste"), (int)LanguageTag::Celeste, nullptr, 1},
        {text("settings.languageDoom"), (int)LanguageTag::Doom, nullptr, 1},
        {text("settings.languageWows"), (int)LanguageTag::WolfOfWallStreet, nullptr, 1},
        {text("settings.languageAmongUs"), (int)LanguageTag::AmongUs, nullptr, 1},
        {text("settings.languageLeet"), (int)LanguageTag::Leet, nullptr, 1},
        {text("settings.languageKlingon"), (int)LanguageTag::Klingon, nullptr, 1},
        {text("settings.languageGopnik"), (int)LanguageTag::Gopnik, nullptr, 1},
        {text("settings.languageShakespeare"), (int)LanguageTag::Shakespeare, nullptr, 1},
    };
    // The captions of the two columns. The list says what the two sides of it
    // are, because a list of languages with sixteen jokes in it does not say it
    // by itself.
    const char* columnTitles[Launcher::kMaxColumns] = {text("settings.languageGroup"),
                                                      text("settings.languageMemeGroup")};
    const SelectPaint languagePaint = drawSelectField(
        ui, ctx, Rect{body.x, rowY, std::fmin(460.0f, body.width), kRowHeight},
        text("settings.language"), languages,
        (int)(sizeof(languages) / sizeof(languages[0])),
        (int)settings_.language, kLanguageSelect, fbWidth, 2, columnTitles);

    // Row 4: rescan + the root actually in use.
    rowY += kRowHeight + 6.0f;
    const Rect rescan{body.x, rowY, buttonWidthFor(ui, text("settings.rescan"), kMinButtonWidth),
                      kRowHeight};
    if (!listOpen && ctx.button(ui, kRescanButton, rescan.x, rescan.y, rescan.width,
                                rescan.height, text("settings.rescan")))
        applySettings();
    drawText(ui, rescan.x + rescan.width + 8.0f, rescan.y + (rescan.height - kTextHeight) * 0.5f,
             projects_.root().c_str(), theme::textDim, fbWidth);

    // Row 5: theme, and row 6: frame rate cap. Both are lists, so each option
    // gets the width it needs and the page stays two controls instead of seven.
    const float selectW = std::fmin(220.0f, body.width);
    rowY += kRowHeight + 6.0f;

    SelectPaint themePaint;
    const SelectItem themes[] = {
        {text("settings.themeDark"), (int)ThemeTag::Dark},
        {text("settings.themeLight"), (int)ThemeTag::Light},
        {text("settings.themeContrast"), (int)ThemeTag::Contrast},
    };
    if (rowY + kFieldHeight * 2.0f + 30.0f < body.y + body.height) {
        themePaint = drawSelectField(ui, ctx, Rect{body.x, rowY, selectW, 0.0f},
                                     text("settings.theme"), themes, 3, (int)settings_.theme,
                                     kThemeSelect, fbWidth);
        rowY += kFieldHeight + kTextHeight + 16.0f;
    }

    SelectPaint capPaint;
    const SelectItem caps[] = {
        {text("settings.fps30"), 30},
        {text("settings.fps60"), 60},
        {text("settings.fps120"), 120},
        {text("settings.fpsOff"), 0},
    };
    if (rowY + kFieldHeight * 2.0f + 30.0f < body.y + body.height) {
        capPaint = drawSelectField(ui, ctx, Rect{body.x, rowY, selectW, 0.0f},
                                   text("settings.frameCap"), caps, 4,
                                   settings_.frameRateCap, kFrameCapSelect, fbWidth);
        rowY += kFieldHeight + kTextHeight + 16.0f;
    }

    // The application icon. A dropdown over the same list the folder holds, so a
    // new icon is one dropped file away - and the window is repainted the moment
    // it is chosen, which is the only way to tell two files apart by name.
    SelectPaint iconPaint;
    SelectItem iconItems[kMaxIconItems] = {};
    int iconCount = 0;
    for (size_t i = 0; i < icons_.size() && iconCount < kMaxIconItems; ++i)
        iconItems[iconCount++] = SelectItem{icons_[i].c_str(), (int)i, icons_[i].c_str()};

    if (iconCount > 0) {
        int current = 0;
        for (int i = 0; i < iconCount; ++i)
            if (icons_[(size_t)iconItems[i].value] == settings_.appIcon) current = i;
        rowY += kRowHeight + 6.0f;
        if (rowY + kFieldHeight * 2.0f + 30.0f < body.y + body.height) {
            iconPaint = drawSelectField(ui, ctx, Rect{body.x, rowY, selectW, 0.0f},
                                        text("settings.icon"), iconItems, iconCount, current,
                                        kIconSelect, fbWidth);
            rowY += kFieldHeight + kTextHeight + 16.0f;
        }
    }

    // Row 6: how many projects the current root yields, so an empty list has a
    // number attached to it instead of looking broken.
    rowY += 6.0f;
    if (rowY + kRowHeight < body.y + body.height) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), text("launcher.settingsSummary"),
                      (int)projects_.projects().size(), (int)settings_.recentProjects.size());
        drawText(ui, body.x, rowY + (kRowHeight - kTextHeight) * 0.5f, buffer, theme::textDim,
                 fbWidth);
    }
    // Said only when there is nothing to pick: an empty dropdown is a control that
    // does not answer, and this one would have no rows at all.
    if (iconCount == 0 && rowY + kRowHeight < body.y + body.height)
        drawText(ui, body.x, rowY + (kRowHeight - kTextHeight) * 0.5f, text("settings.iconNone"),
                 theme::textDim, fbWidth);

    // The open list, last: over every row above it, and after a flush. Only one
    // select can be open, so at most one of these draws anything.
    const auto paintOpen = [&](const SelectPaint& paint) {
        if (paint.id < 0 || openSelect_ != paint.id) return;
        ui.flush();
        const int chosen = drawSelectList(ui, ctx, paint, body, fbWidth);
        if (chosen < 0) return;
        if (paint.id == kLanguageSelect) setLanguage((LanguageTag)chosen);
        else if (paint.id == kThemeSelect) setTheme((theme::ThemeId)chosen);
        else if (paint.id == kFrameCapSelect) setFrameRateCap(chosen);
        else if (paint.id == kIconSelect) setAppIcon(chosen, window);
    };
    paintOpen(languagePaint);
    paintOpen(themePaint);
    paintOpen(capPaint);
    paintOpen(iconPaint);
}

void Launcher::drawAccount(UiRenderer& ui, UiContext& ctx, int fbWidth) {
    const Rect& area = layout_.content;
    if (area.width <= 0.0f) return;
    textClipRight_ = area.x + area.width - 6.0f;

    float y = 0.0f;
    const Rect body = section(ui, ctx, area, text("launcher.navAccount"), nullptr, fbWidth, y);
    if (body.width <= 0.0f) return;
    y = body.y;

    drawFieldRow(ui, fbWidth, y, text("launcher.accountName"), text("launcher.accountLocal"),
                 theme::text);
    drawFieldRow(ui, fbWidth, y, text("launcher.accountType"), text("launcher.accountOffline"),
                 theme::text);
    drawFieldRow(ui, fbWidth, y, text("launcher.accountEngine"), versionLabel_.c_str(),
                 theme::textDim);
    drawFieldRow(ui, fbWidth, y, text("launcher.accountProjects"),
                 std::to_string(projects_.projects().size()).c_str(), theme::textDim);
    drawFieldRow(ui, fbWidth, y, text("launcher.accountRoot"), projects_.root().c_str(),
                 theme::textDim);

    if (y + kTextHeight < body.y + body.height) {
        y += 10.0f;
        drawText(ui, body.x, y, text("launcher.accountNote"), theme::textDim, fbWidth);
    }
}

void Launcher::drawFooter(UiRenderer& ui, UiContext& ctx, int fbWidth) {
    const Rect& footer = layout_.footer;
    if (footer.height <= 0.0f) return;
    textClipRight_ = -1.0f;

    ui.fillRect(footer.x, footer.y, footer.width, 1.0f, theme::separator);
    const float buttonW = buttonWidth(footer.width - 2.0f * kPadding, 0.3f, kButtonWidth);
    const Rect quit{footer.x + footer.width - buttonW - kPadding,
                    footer.y + (footer.height - kRowHeight) * 0.5f, buttonW, kRowHeight};
    if (ctx.button(ui, kQuitButton, quit.x, quit.y, quit.width, quit.height, text("launcher.quit")))
        quitRequested_ = true;

    const char* hint = view_ == View::NewProject ? text("launcher.hintNewProject")
                                                  : text("launcher.hints");
    drawText(ui, footer.x + kPadding, footer.y + (footer.height - kTextHeight) * 0.5f, hint,
             theme::textDim, fbWidth);
}

// --- delete confirmation ----------------------------------------------------

void Launcher::drawDeleteConfirm(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth,
                                 int fbHeight) {
    const float fw = (float)fbWidth;
    const float fh = (float)fbHeight;

    // Esc is "no". Closing the window with the titlebar X never reaches this
    // code as an answer either - the process just goes away, and nothing is
    // removed. Only the Ok button below deletes anything.
    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        cancelDelete();
        return;
    }

    if (pendingDelete_ < 0 || pendingDelete_ >= (int)visible_.size()) {
        // The project vanished under the dialog (a rescan, a manual delete).
        cancelDelete();
        return;
    }
    const size_t listRow = (size_t)pendingDelete_;
    const size_t projectIndex = (size_t)visible_[listRow];
    const ProjectInfo& info = projects_.projects()[projectIndex];

    ui.fillRect(0.0f, 0.0f, fw, fh, Color{0.0f, 0.0f, 0.0f, 0.65f});

    const float panelW = std::fmin(480.0f, fw - 24.0f);
    const float panelH = std::fmin(178.0f, fh - 24.0f);
    const Rect panel =
        intersect(Rect{(fw - panelW) * 0.5f, (fh - panelH) * 0.5f, panelW, panelH}, fw, fh);
    if (panel.width <= 0.0f || panel.height <= 0.0f) return;

    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    ui.fillRect(panel.x, panel.y, panel.width, 2.0f, theme::error);

    textClipRight_ = panel.x + panel.width - kPadding;
    drawText(ui, panel.x + kPadding, panel.y + 14.0f, text("launcher.deleteTitle"), theme::text,
             fbWidth);

    // The question names the project, because "this project" next to a list of
    // several is not an answer the user can be sure about.
    char question[512];
    std::snprintf(question, sizeof(question), text("launcher.deleteQuestion"), info.name.c_str());
    drawText(ui, panel.x + kPadding, panel.y + 14.0f + kTextHeight + 8.0f, question, theme::text,
             fbWidth);

    drawTextFit(ui, panel.x + kPadding, panel.y + 14.0f + 2.0f * kTextHeight + 12.0f,
                panel.width - 2.0f * kPadding, /*keepTail=*/true, info.path, theme::textDim,
                fbWidth);

    drawText(ui, panel.x + kPadding, panel.y + 14.0f + 3.0f * kTextHeight + 14.0f,
             text("launcher.deleteIrreversible"), theme::warning, fbWidth);

    // Ok on the right, Return on the left: Ok is the destructive one, so it does
    // not sit where the Return key's muscle memory is. Widths come from the
    // labels - "Повернутись" is wider than the default button.
    const char* okLabel = text("launcher.deleteOk");
    const char* backLabel = text("launcher.deleteReturn");
    const float buttonW = std::fmin(panel.width * 0.44f,
                                    std::fmax(kButtonWidth,
                                              std::fmax(ui.textWidth(okLabel),
                                                        ui.textWidth(backLabel)) + 26.0f));
    const float buttonY = panel.y + panel.height - kRowHeight - 14.0f;
    const Rect ok{panel.x + panel.width - buttonW - kPadding, buttonY, buttonW, kRowHeight};
    const Rect back{ok.x - buttonW - 8.0f, buttonY, buttonW, kRowHeight};
    if (ctx.button(ui, kDeleteCancelButton, back.x, back.y, back.width, back.height,
                   text("launcher.deleteReturn")))
        cancelDelete();
    if (ctx.button(ui, kDeleteConfirmButton, ok.x, ok.y, ok.width, ok.height,
                   text("launcher.deleteOk")))
        confirmDelete();
}

// --- folder picker ----------------------------------------------------------

void Launcher::openPicker() {
    // The shell's own folder dialog first. The in-app browser below is only a
    // fallback for platforms that have none, so on Windows this is Explorer.
    std::string chosen;
    const fs::PickResult picked = fs::pickFolder(rootDraft_, chosen);
    if (picked == fs::PickResult::Picked) {
        applyRoot(chosen);
        return;
    }
    if (picked == fs::PickResult::Cancelled) return;  // the user changed their mind

    lastError_ = text("settings.folderNoDialog");
    openFolderBrowser();
}

void Launcher::openFolderBrowser() {
    pickerPath_ = rootDraft_.empty() ? Settings::defaultProjectRoot() : rootDraft_;
    if (!fs::isDirectory(pickerPath_)) {
        const std::string parent = parentPath(pickerPath_);
        if (!parent.empty()) pickerPath_ = parent;
    }
    pickerSelected_ = 0;
    pickerMessage_.clear();
    refreshPicker();
    pickerOpen_ = true;
}

void Launcher::closePicker() {
    pickerOpen_ = false;
    pickerMessage_.clear();
    nameField_.editing = false;
    rootField_.editing = false;
}

void Launcher::refreshPicker() {
    pickerEntries_.clear();
    std::vector<fs::DirEntry> entries;
    if (!fs::listDirectoryEx(pickerPath_, entries)) {
        pickerMessage_ = text("settings.folderUnreadable");
    } else {
        for (const fs::DirEntry& entry : entries)
            if (entry.isDir) pickerEntries_.push_back(entry);
    }
    const int rows = (int)pickerEntries_.size() + 2;  // ".." + folders + "choose"
    pickerSelected_ = std::clamp(pickerSelected_, 0, rows - 1);
}

void Launcher::movePickerSelection(int delta) {
    const int rows = (int)pickerEntries_.size() + 2;
    pickerSelected_ = std::clamp(pickerSelected_ + delta, 0, rows - 1);
}

void Launcher::activatePickerRow() {
    const int last = (int)pickerEntries_.size() + 1;  // index of the "choose" row
    if (pickerSelected_ == 0) {
        const std::string parent = parentPath(pickerPath_);
        if (parent.empty() || parent == pickerPath_) {
            pickerMessage_ = text("settings.folderTop");
            return;
        }
        pickerPath_ = parent;
        pickerSelected_ = 0;
        refreshPicker();
        return;
    }
    if (pickerSelected_ == last) {
        closePicker();
        applyRoot(pickerPath_);
        return;
    }
    pickerPath_ = str::join(pickerPath_, pickerEntries_[(size_t)pickerSelected_ - 1].name);
    pickerSelected_ = 0;
    refreshPicker();
}

void Launcher::drawFolderPicker(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth,
                                int fbHeight) {
    const float fw = (float)fbWidth;
    const float fh = (float)fbHeight;

    // Modal: Esc cancels, Enter walks down and chooses, arrows move.
    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        closePicker();
        return;
    }
    if (window.takeKeyPress(GLFW_KEY_UP)) movePickerSelection(-1);
    if (window.takeKeyPress(GLFW_KEY_DOWN)) movePickerSelection(1);
    if (window.takeKeyPress(GLFW_KEY_ENTER) || window.takeKeyPress(GLFW_KEY_KP_ENTER))
        activatePickerRow();

    ui.fillRect(0.0f, 0.0f, fw, fh, Color{0.0f, 0.0f, 0.0f, 0.6f});

    const float rowsH = std::fmin(240.0f, ((float)pickerEntries_.size() + 2.0f) * kRowHeight);
    const float panelW = std::fmin(560.0f, fw - 24.0f);
    const float panelH = std::fmin(fh - 24.0f, 104.0f + rowsH);
    const Rect panel = intersect(
        Rect{(fw - panelW) * 0.5f, (fh - panelH) * 0.5f, panelW, panelH}, fw, fh);
    if (panel.width <= 0.0f || panel.height <= 0.0f) return;

    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    drawText(ui, panel.x + 12.0f, panel.y + 10.0f, text("settings.chooseFolder"), theme::text,
             fbWidth);

    // Current folder, shrunk to fit rather than cut from the left, so the leaf stays
    // visible even in a deep path.
    drawTextFit(ui, panel.x + 12.0f, panel.y + 30.0f, panel.width - 24.0f, /*keepTail=*/true,
                pickerPath_, theme::accent, fbWidth);
    ui.fillRect(panel.x + 12.0f, panel.y + 48.0f, std::fmax(0.0f, panel.width - 24.0f), 1.0f,
                theme::separator);

    const float rowsTop = panel.y + 56.0f;
    const float bodyH = std::fmax(0.0f, panel.height - 56.0f - kRowHeight - 8.0f);
    const int visible = bodyH > 0.0f ? (int)(bodyH / kRowHeight) : 0;
    const int first = std::clamp(pickerSelected_ - visible / 2, 0,
                                 std::max(0, (int)pickerEntries_.size() + 1 - visible));

    for (int offsetRow = 0; offsetRow < visible; ++offsetRow) {
        const int index = first + offsetRow;
        if (index > (int)pickerEntries_.size() + 1) break;
        const Rect row{panel.x + 12.0f, rowsTop + (float)offsetRow * kRowHeight,
                       std::fmax(0.0f, panel.width - 24.0f), kRowHeight - 1.0f};
        const char* label = "";
        if (index == 0)
            label = "..";
        else if (index == (int)pickerEntries_.size() + 1)
            label = text("settings.chooseHere");
        else
            label = pickerEntries_[(size_t)index - 1].name.c_str();

        const bool isLast = index == (int)pickerEntries_.size() + 1;
        const bool hovered = ctx.hovered(row);
        drawRow(ui, row, index == pickerSelected_, hovered, label,
                isLast ? text("settings.chooseHereHint") : nullptr, fbWidth);
        if (hovered && ctx.pointerPressed()) {
            pickerSelected_ = index;
            activatePickerRow();
            // The list may have been replaced (descend, go up, choose), so the
            // remaining rows of this frame are no longer the rows on screen.
            break;
        }
    }

    const Rect cancel{panel.x + 12.0f, panel.y + panel.height - kRowHeight - 6.0f,
                      buttonWidth(std::fmax(0.0f, panel.width - 24.0f), 0.3f, kButtonWidth),
                      kRowHeight};
    if (ctx.button(ui, kPickerCancelButton, cancel.x, cancel.y, cancel.width, cancel.height,
                   text("settings.cancel")))
        closePicker();

    if (!pickerMessage_.empty())
        drawText(ui, cancel.x + cancel.width + 8.0f,
                 cancel.y + (cancel.height - kTextHeight) * 0.5f, pickerMessage_.c_str(),
                 theme::warning, fbWidth);
}

// --- input ------------------------------------------------------------------

void Launcher::handleKeys(Window& window) {
    // A confirmation is modal: only Esc (handled in its own draw) may act, and
    // it means "no". Enter deliberately does nothing here so a stray press
    // cannot confirm a deletion.
    if (deleteOpen_) return;

    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        // An open list is the first thing Esc closes, so a stray Esc while
        // choosing a setting never leaves the page or the app.
        if (openSelect_ >= 0) {
            openSelect_ = -1;
            selectHoverRow_ = -1;
            return;
        }
        // Back out of a sub-view first, and only leave the app from the list.
        if (view_ != View::Projects && view_ != View::Favorites) {
            setView(View::Projects);
            return;
        }
        quitRequested_ = true;
        return;
    }

    const bool typing = nameField_.editing || rootField_.editing;

    // An open dropdown owns the arrows while it is up. Without this the list
    // scrolls with the wheel but not with the keyboard, and the project list
    // behind it would move on every keypress - visible scrolling happening
    // somewhere other than where the pointer is.
    if (openSelect_ >= 0) {
        const int step = window.takeKeyPress(GLFW_KEY_DOWN) ? 1
                        : (window.takeKeyPress(GLFW_KEY_UP) ? -1 : 0);
        if (step != 0) {
            selectScroll_ = std::clamp(selectScroll_ + step, 0, openSelectScrollMax_);
            return;
        }
        if (window.takeKeyPress(GLFW_KEY_PAGE_DOWN)) {
            selectScroll_ = std::clamp(selectScroll_ + openSelectRows_, 0, openSelectScrollMax_);
            return;
        }
        if (window.takeKeyPress(GLFW_KEY_PAGE_UP)) {
            selectScroll_ = std::clamp(selectScroll_ - openSelectRows_, 0, openSelectScrollMax_);
            return;
        }
        if (window.takeKeyPress(GLFW_KEY_HOME)) {
            selectScroll_ = 0;
            return;
        }
        if (window.takeKeyPress(GLFW_KEY_END)) {
            selectScroll_ = openSelectScrollMax_;
            return;
        }
    }

    // Arrows belong to the text field while it has the focus, so they are
    // consumed either way - otherwise they would move the project selection
    // behind the caret.
    if (window.takeKeyPress(GLFW_KEY_UP) && !typing) moveSelection(-1);
    if (window.takeKeyPress(GLFW_KEY_DOWN) && !typing) moveSelection(1);

    const bool enter =
        window.takeKeyPress(GLFW_KEY_ENTER) || window.takeKeyPress(GLFW_KEY_KP_ENTER);
    if (!enter) return;

    if (nameField_.editing && !nameField_.empty()) {
        createProject();
        return;
    }
    if (rootField_.editing && !rootField_.empty()) {
        applyRoot(rootDraft_);
        return;
    }
    const int project = selectedProject();
    if (project >= 0) {
        // An empty name field must not swallow Enter: the user just wants to open
        // the highlighted project.
        nameField_.editing = false;
        rootField_.editing = false;
        requestOpen(projects_.projects()[(size_t)project].path);
    }
}

void Launcher::moveSelection(int delta) {
    const int count = (int)visible_.size();
    if (count <= 0) return;
    // From "nothing selected", Down lands on the first project and Up on the
    // last, instead of skipping the first one.
    const int base = selected_ < 0 ? (delta > 0 ? -1 : count) : selected_;
    selected_ = std::clamp(base + delta, 0, count - 1);

    const int rows = listRows();
    if (selected_ < listScroll_) listScroll_ = selected_;
    if (rows > 0 && selected_ >= listScroll_ + rows) listScroll_ = selected_ - rows + 1;
    listScroll_ = std::clamp(listScroll_, 0, maxListScroll());
}

void Launcher::clampSelection() {
    const int count = (int)visible_.size();
    // -1 is "nothing selected" and survives a rescan on purpose: Enter must not
    // open a project nobody picked.
    if (selected_ >= count) selected_ = count - 1;
    listScroll_ = std::clamp(listScroll_, 0, maxListScroll());
}

void Launcher::toggleFavorite(int index) {
    if (index < 0 || index >= (int)visible_.size()) return;
    // projects() hands out a const view, and the star now lives on disk, so the
    // only thing worth holding is the path to rewrite.
    const ProjectInfo info = projects_.projects()[(size_t)visible_[(size_t)index]];
    const bool now = projects_.setFavorite(info.path, !info.favorite);

    projects_.scan();
    rebuildVisible();
    if (view_ == View::Favorites && !now) {
        // Unstarring from the Favorites tab removes the row from under the
        // cursor. Land on the neighbour instead of on nothing.
        if (selected_ >= (int)visible_.size()) selected_ = (int)visible_.size() - 1;
    }
    clampSelection();
    moveSelection(0);
}

void Launcher::askDelete(int row) {
    if (row < 0 || row >= (int)visible_.size()) return;
    pendingDelete_ = row;
    deleteOpen_ = true;
    lastError_.clear();
}

void Launcher::cancelDelete() {
    deleteOpen_ = false;
    pendingDelete_ = -1;
}

void Launcher::confirmDelete() {
    if (!deleteOpen_ || pendingDelete_ < 0 || pendingDelete_ >= (int)visible_.size()) {
        cancelDelete();
        return;
    }
    const ProjectInfo target = projects_.projects()[(size_t)visible_[(size_t)pendingDelete_]];
    const std::string path = target.path;
    const std::string name = target.name;
    const int row = pendingDelete_;
    cancelDelete();

    if (!fs::removeTree(path)) {
        lastError_ = text("launcher.deleteFailed");
        Log::error("Launcher: could not delete '%s'", path.c_str());
        return;
    }

    // A deleted project must not stay in the recents either: every other entry
    // there is clickable and this one would only produce an error.
    settings_.forgetProject(path);
    settings_.save();

    projects_.scan();
    rebuildVisible();
    if (selected_ >= row) selected_ = std::max(0, row - 1);
    clampSelection();
    lastError_.clear();
    Log::info("Launcher: deleted project '%s'", name.c_str());
}

// --- actions ----------------------------------------------------------------

void Launcher::createProject() {
    const std::string name = str::trim(nameField_.text());
    const ProjectInfo info = projects_.create(name, settings_.language);
    if (!info.valid) {
        lastError_ = text("launcher.createFailed");
        return;
    }

    nameField_.clear();
    lastError_.clear();
    projects_.scan();
    setView(View::Projects);
    rebuildVisible();

    // Select what was just created, then open it - a new project exists to be
    // worked on, not to be listed.
    for (size_t index = 0; index < visible_.size(); ++index) {
        if (str::equalsIgnoreAscii(projects_.projects()[(size_t)visible_[index]].path, info.path)) {
            selected_ = (int)index;
            moveSelection(0);
            break;
        }
    }
    clampSelection();
    requestOpen(info.path);
}

void Launcher::requestOpen(const std::string& path) {
    if (!projectExists(path)) {
        lastError_ = text("launcher.missingProject");
        return;
    }
    requestedOpen_ = true;
    requestedPath_ = path;
    lastError_.clear();
}

bool Launcher::openProject(const std::string& path, std::string& out) {
    if (!projectExists(path)) {
        Log::error("Launcher: '%s' is not a project", path.c_str());
        lastError_ = text("launcher.missingProject");
        return false;
    }

    settings_.rememberProject(path);
    settings_.save();
    out = path;
    lastError_.clear();
    Log::info("Launcher: opening '%s'", path.c_str());
    return true;
}

void Launcher::applyRoot(const std::string& root) {
    const std::string clean = str::trim(root);
    if (clean.empty()) {
        lastError_ = text("settings.rootEmpty");
        return;
    }

    settings_.projectRoot = clean;
    rootDraft_ = clean;
    rootField_.clear();
    if (clean.size() < sizeof(rootField_.buffer) - 1) rootField_.set(clean.c_str());
    rootField_.editing = false;

    lastError_.clear();
    applySettings();  // setRoot + rescan + save
}

void Launcher::setLanguage(LanguageTag tag) {
    settings_.language = tag;
    applySettings();  // pushes the language into the localizer
}

void Launcher::setTheme(theme::ThemeId id) {
    const ThemeTag tag = themeTagFromTheme(id);
    if (settings_.theme == tag) return;
    settings_.theme = tag;
    settings_.save();
    theme::setActiveTheme(id);
}

void Launcher::scanIcons() {
    icons_.clear();
    // The icons are in the assets of the engine - in a built game that is the
    // `plugins` folder of the data - and the asset root is where they are.
    // .ico only: the shell loads those natively at every size the title bar and the
    // task bar ask for, and a PNG here would be a choice the picker offers and the
    // window cannot show.
    const std::string assets = fs::assetRoot();
    const std::string dir =
        assets.empty() ? str::join(fs::executableDir(), kAppIconsDir)
                        : str::join(assets, "image/icons");
    for (const std::string& path : fs::listFilesByExtension(dir, {".ico"}))
        icons_.push_back(str::fileName(path));

    // The shipped default first, so a folder whose only file is not the default
    // still opens on something, and so the list order is stable across machines
    // that added different extra icons.
    for (size_t i = 0; i < icons_.size(); ++i) {
        if (icons_[i] != kAppIconFile) continue;
        icons_.erase(icons_.begin() + (long)i);
        icons_.insert(icons_.begin(), kAppIconFile);
        break;
    }
    if (icons_.empty())
        Log::warn("Launcher: no icon files in '%s' - falling back to the drawn mark",
                  dir.c_str());
    else
        Log::info("Launcher: %d icon file(s) in '%s'", (int)icons_.size(), dir.c_str());
}

void Launcher::setAppIcon(int index, Window& window) {
    if (index < 0 || index >= (int)icons_.size()) return;
    const std::string name = icons_[(size_t)index];
    if (name == settings_.appIcon) return;
    settings_.appIcon = name;
    settings_.save();
    // Straight away, not on the next launch: the point of the setting is to see
    // which icon is which, and a title bar that only changes after a restart is a
    // settings screen that has to be trusted instead of used.
    window.applyIcon(name.c_str());
    Log::info("Launcher: application icon '%s'", name.c_str());
}

void Launcher::setFrameRateCap(int cap) {
    if (settings_.frameRateCap == cap) return;
    settings_.frameRateCap = cap;
    settings_.save();
    // The running loop reads the cap every frame, so nothing else has to happen
    // here - the next frame is already paced differently.
    Log::info("Launcher: frame cap %d fps", cap);
}

const char* Launcher::nameHint() const {
    const std::string name = str::trim(nameField_.text());
    if (name.empty()) return text("launcher.nameHint");
    if (name.size() < 3) return text("launcher.nameTooShort");
    if (name.size() > 48) return text("launcher.nameTooLong");

    bool badChars = false;
    for (const char c : name)
        if (isSeparator(c) || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            badChars = true;
    if (badChars || name.find("..") != std::string::npos) return text("launcher.nameBadChars");
    if (!ProjectLibrary::isValidName(name)) return text("launcher.nameBadChars");

    for (const ProjectInfo& info : projects_.projects())
        if (str::equalsIgnoreAscii(info.name, name)) return text("launcher.nameTaken");
    return text("launcher.nameOk");
}

// --- drawing helpers --------------------------------------------------------

float Launcher::fitScale(UiRenderer& ui, const std::string& value, float maxWidth) const {
    if (value.empty() || maxWidth <= 0.0f) return 1.0f;
    const float full = ui.textWidth(value.c_str());
    if (full <= maxWidth) return 1.0f;
    // Width is linear in the scale, so one division gives the exact factor that
    // fits - no search loop and no second pass over the string.
    return std::fmin(1.0f, maxWidth / full);
}

void Launcher::drawText(UiRenderer& ui, float x, float y, const char* value, const Color& color,
                        int fbWidth) const {
    drawTextFit(ui, x, y, 0.0f, /*keepTail=*/false, value ? value : "", color, fbWidth);
}

void Launcher::drawTextFit(UiRenderer& ui, float x, float y, float maxWidth, bool keepTail,
                           const std::string& value, const Color& color, int fbWidth) const {
    if (value.empty()) return;

    // Clip to the framebuffer and, when a section is active, to that section's
    // right edge: a long hint used to spill into the neighbouring column.
    float limit = (float)fbWidth;
    if (textClipRight_ > 0.0f) limit = std::fmin(limit, textClipRight_);
    if (x >= limit) return;
    if (x < 0.0f) x = 0.0f;
    if (maxWidth > 0.0f) limit = std::fmin(limit, x + maxWidth);

    // Shrink first. A path that ends in "..." tells the user nothing, while the
    // same path a few pixels smaller tells them all of it; characters are only
    // dropped once shrinking has hit its floor.
    const float scale = fitScale(ui, value, limit - x);
    if (scale >= UiRenderer::kMinTextScale) {
        ui.textScaled(x, y, value.c_str(), color, scale);
        return;
    }

const float floor_ = UiRenderer::kMinTextScale;
    const char* ellipsis = "...";
    const float ellipsisWidth = ui.textWidthScaled(ellipsis, floor_);

    if (keepTail) {
        // Keep the tail: for a path the last component is what is being looked for.
        // fitOffset already returns the longest byte offset whose suffix fits at
        // full scale - and it walks whole code points, never splitting a glyph -
        // so the suffix fits at floor_ too, which is smaller. No second pass.
        std::string tail = value.substr(fitOffset(ui, value, limit - x - ellipsisWidth));
        if (tail.empty()) return;
        ui.textScaled(x + ellipsisWidth, y, tail.c_str(), color, floor_);
        ui.textScaled(x, y, ellipsis, color, floor_);
        return;
    }

    // Keep the head: which sentence it is matters more than where it ends. The
    // old loop popped one character and re-measured the whole remaining string
    // each time, so a long path cost O(n^2) glyph lookups and one heap temporary
    // per character - once per project row, every frame. One linear pass over the
    // code points gives the same answer.
    const size_t headLength = fitHeadLength(ui, value, limit - x - ellipsisWidth, floor_);
    if (headLength == 0) return;
    std::string head(value.c_str(), headLength);
    ui.textScaled(x, y, (head + ellipsis).c_str(), color, floor_);
}

void Launcher::drawTextRight(UiRenderer& ui, float right, float y, const char* value,
                             const Color& color, int fbWidth) const {
    if (!value || !*value) return;
    drawText(ui, right - ui.textWidth(value), y, value, color, fbWidth);
}

void Launcher::drawTextCentered(UiRenderer& ui, const Rect& rect, float y, const char* value,
                                const Color& color) const {
    if (!value || !*value || rect.width <= 0.0f) return;
    ui.text(rect.x + (rect.width - ui.textWidth(value)) * 0.5f, y, value, color);
}

void Launcher::drawRow(UiRenderer& ui, const Rect& rect, bool selected, bool hovered,
                       const char* name, const char* info, int fbWidth) const {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;

    Color background = theme::panel;
    if (selected)
        background = theme::accentDim;
    else if (hovered)
        background = theme::buttonHover;
    ui.fillRect(rect.x, rect.y, rect.width, rect.height, background);
    if (selected) ui.fillRect(rect.x, rect.y, 2.0f, rect.height, theme::accent);

    const float textY = rect.y + (rect.height - kTextHeight) * 0.5f;
    drawText(ui, rect.x + 8.0f, textY, name, theme::text, fbWidth);

    if (info && *info) {
        // The right column only appears when it cannot collide with the name.
        const float infoX = rect.x + rect.width - 8.0f - ui.textWidth(info);
        if (infoX > rect.x + 8.0f + ui.textWidth(name ? name : "") + 12.0f)
            drawText(ui, infoX, textY, info, theme::textDim, fbWidth);
    }
}

void Launcher::drawProjectIcon(UiRenderer& ui, const Rect& rect, bool selected,
                               bool favorite) const {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    const Color line = favorite ? theme::warning : (selected ? theme::accent : theme::textDim);
    const Color face = line.scaled(0.35f);

    // A little isometric cube: three filled faces plus a stroke, drawn from
    // three parallelograms made of thin rects. No texture, no icon font.
    const float w = rect.width;
    const float h = rect.height;
    const float cx = rect.x + w * 0.5f;
    const float cy = rect.y + h * 0.5f;
    const float hw = w * 0.5f;
    const float hh = h * 0.28f;
    const float dy = h * 0.22f;

    // Top rhombus, then the two side faces, then the outline.
    ui.fillRect(cx - hw * 0.5f, cy - dy - hh * 0.5f, hw, hh * 0.5f, face);
    ui.fillRect(cx, cy - dy, hw * 0.5f, hh, face.scaled(0.75f));
    ui.fillRect(cx - hw, cy - dy + hh * 0.5f, hw * 0.5f, hh, face.scaled(0.55f));
    ui.fillRect(cx - hw * 0.5f, cy + dy, hw * 0.5f, h * 0.18f, face.scaled(0.45f));
    ui.fillRect(cx, cy + dy + h * 0.06f, hw * 0.5f, h * 0.18f, face.scaled(0.35f));

    const float t = 1.0f;
    ui.fillRect(cx - hw * 0.5f, cy - dy - hh * 0.5f, hw, t, line);          // top edge
    ui.fillRect(cx - hw * 0.5f, cy + dy - hh * 0.5f, t, hh + dy, line);     // left edge
    ui.fillRect(cx + hw * 0.5f - t, cy + dy - hh * 0.5f, t, hh + dy, line); // right edge
    ui.fillRect(cx - hw * 0.5f, cy + dy - hh * 0.5f, hw, t, line);          // bottom edge
}

void Launcher::drawStar(UiRenderer& ui, UiContext& ctx, int id, const Rect& rect, bool on,
                        const Color& background) const {
    if (rect.width <= 0.0f) return;
    if (ctx.hovered(rect)) ctx.markHovered(id);
    const bool hovered = ctx.hovered(rect);
    const Color color = on ? theme::warning : (hovered ? theme::text : theme::textDim);
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float r = rect.width * 0.32f;

    // A five-point star needs rotated primitives, which this batcher does not
    // have. A ring reads as "mark me" at 20 pixels and needs none: an outer disc
    // in the colour, then the row background punched back out of the middle.
    ui.fillCircle(cx, cy, r, color);
    ui.fillCircle(cx, cy, on ? r * 0.42f : r * 0.62f, background);
}

// Width this label needs, with padding. Text is measured, never guessed - the
    // two languages differ by more than a factor of two on the same label.
float Launcher::buttonWidthFor(UiRenderer& ui, const char* label, float minimum) const {
    return std::fmax(minimum, ui.textWidth(label) + 18.0f);
}

void Launcher::drawGear(UiRenderer& ui, const Rect& rect, bool active, bool hovered) const {
    if (rect.width <= 0.0f) return;
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float r = rect.width * 0.24f;
    const Color color = active ? theme::accent : (hovered ? theme::text : theme::textDim);

    // Teeth first, ring second: the ring then hides the middle of each bar and
    // only the four ends stick out, which is the whole silhouette of a gear.
    const float t = std::fmax(1.5f, r * 0.42f);
    const float len = r * 2.7f;
    ui.fillRect(cx - len * 0.5f, cy - t * 0.5f, len, t, color);
    ui.fillRect(cx - t * 0.5f, cy - len * 0.5f, t, len, color);
    ui.fillCircle(cx, cy, r, color);
    ui.fillCircle(cx, cy, r * 0.45f, theme::panel);
}

void Launcher::drawFieldRow(UiRenderer& ui, int fbWidth, float& y, const char* caption,
                            const char* value, const Color& valueColor) {
    if (y + kTextHeight * 2.0f > layout_.content.y + layout_.content.height) return;
    const float left = layout_.content.x + kPadding;
    const float slot = layout_.content.width - 2.0f * kPadding;
    drawTextFit(ui, left, y, slot, /*keepTail=*/false, caption, theme::textDim, fbWidth);
    drawTextFit(ui, left, y + kTextHeight + 2.0f, slot, /*keepTail=*/true, value ? value : "",
                valueColor, fbWidth);
    y += kTextHeight * 2.0f + 8.0f;
}

void Launcher::drawField(UiRenderer& ui, const Rect& rect, const TextField& field,
                         const char* placeholder, int fbWidth) const {
    if (rect.width <= 0.0f) return;
    const float textY = rect.y + (rect.height - kTextHeight) * 0.5f;

    if (field.empty()) {
        ui.fillRect(rect.x, rect.y, rect.width, rect.height,
                    field.editing ? theme::accent.scaled(0.35f) : theme::field);
        drawText(ui, rect.x + 6.0f, textY, placeholder, theme::textDim, fbWidth);
        return;
    }
    drawFieldValue(ui, rect, field.text(), field.editing, /*asPath=*/false, fbWidth);
}

void Launcher::drawFieldValue(UiRenderer& ui, const Rect& rect, const char* value, bool editing,
                              bool asPath, int fbWidth) const {
    if (rect.width <= 0.0f) return;
    ui.fillRect(rect.x, rect.y, rect.width, rect.height,
                editing ? theme::accent.scaled(0.35f) : theme::field);

    const float inner = std::fmax(0.0f, rect.width - 12.0f);
    const std::string shown = value ? value : "";

    // Shrunk to fit, and only cut when even the smallest size is too wide. A
    // path is shown whole whenever it can be: which folder this is, is the whole
    // point of the field.
    const float scale = std::fmax(fitScale(ui, shown, inner), UiRenderer::kMinTextScale);
    const float textY = rect.y + (rect.height - kTextHeight) * 0.5f;
    drawTextFit(ui, rect.x + 6.0f, textY, inner, /*keepTail=*/true, shown, theme::text, fbWidth);

    if (editing) {
        const float caretX = rect.x + 6.0f + ui.textWidthScaled(shown.c_str(), scale) + 1.0f;
        ui.fillRect(caretX, rect.y + 3.0f, 1.5f, std::fmax(0.0f, rect.height - 6.0f),
                    theme::accent);
    }
}

void Launcher::drawDisabledButton(UiRenderer& ui, UiContext& ctx, int id, const Rect& rect,
                                  const char* label, int fbWidth) const {
    ui.fillRect(rect.x, rect.y, rect.width, rect.height, theme::field);
    if (ctx.hovered(rect)) ctx.markHovered(id);
    const float width = ui.textWidth(label);
    drawText(ui, std::fmax(rect.x + 3.0f, rect.x + (rect.width - width) * 0.5f),
             rect.y + (rect.height - kTextHeight) * 0.5f, label, theme::textDim, fbWidth);
}

Rect Launcher::section(UiRenderer& ui, UiContext& ctx, const Rect& area, const char* title,
                       const char* trailing, int fbWidth, float& contentY) const {
    ctx.sectionHeader(ui, area.x, area.y, area.width, title);
    const float bodyTop = area.y + kSectionHeader;
    ui.fillRect(area.x, bodyTop, area.width, std::fmax(0.0f, area.y + area.height - bodyTop),
                theme::panelDark);

    if (trailing) drawTextRight(ui, area.x + area.width - 8.0f, area.y + 4.0f, trailing,
                                theme::textDim, fbWidth);

    contentY = bodyTop + kContentInset;
    return Rect{area.x + 8.0f, contentY, std::fmax(0.0f, area.width - 16.0f),
                std::fmax(0.0f, area.y + area.height - contentY - 6.0f)};
}

const char* Launcher::sectionTitle() const {
    return view_ == View::Favorites ? text("launcher.tabFavorites") : text("launcher.navProjects");
}

void Launcher::setView(View view) {
    if (view_ == view) return;
    view_ = view;

    // Each list needs its own selection: switching to Favorites with the third
    // All project highlighted would otherwise highlight whatever happens to sit
    // at index 2 in a completely different set.
    if (view == View::Favorites || view == View::Projects) {
        selected_ = -1;
        listScroll_ = 0;
        rebuildVisible();
        // Preselect the first row so Enter works immediately, the same promise
        // the initial scan makes.
        if (selected_ < 0 && !visible_.empty()) selected_ = 0;
    }
    lastError_.clear();
}

size_t Launcher::fitHeadLength(UiRenderer& ui, const std::string& value, float maxWidth,
                               float scale) const {
    // Byte length of the longest prefix of `value` that fits in `maxWidth` at
    // `scale`, measured by walking whole code points - so it never cuts a UTF-8
    // sequence in half. Width grows monotonically with the prefix, so stopping at
    // the first glyph that does not fit is the answer; no search is needed and no
    // substring is ever built to measure it.
    if (maxWidth <= 0.0f) return 0;
    const char* const begin = value.c_str();
    const char* p = begin;
    const char* const end = begin + value.size();
    char glyph[8];
    float width = 0.0f;

    while (p < end) {
        const char* const start = p;
        str::nextCodepoint(p);
        size_t length = (size_t)(p - start);
        if (length >= sizeof(glyph)) length = sizeof(glyph) - 1;
        std::memcpy(glyph, start, length);
        glyph[length] = '\0';

        const float advance = ui.textWidthScaled(glyph, scale);
        if (width + advance > maxWidth) return (size_t)(start - begin);
        width += advance;
    }
    return value.size();
}

size_t Launcher::fitOffset(UiRenderer& ui, const std::string& value, float maxWidth) const {
    if (maxWidth <= 0.0f) return value.size();
    const char* const begin = value.c_str();
    const char* p = begin;
    const char* const end = begin + value.size();
    char glyph[8];
    float width = 0.0f;
    size_t offset = 0;

    while (p < end) {
        const char* const start = p;
        str::nextCodepoint(p);  // advances past one UTF-8 code point
        size_t length = (size_t)(p - start);
        if (length >= sizeof(glyph)) length = sizeof(glyph) - 1;
        std::memcpy(glyph, start, length);
        glyph[length] = '\0';

        const float advance = ui.textWidth(glyph);
        if (width + advance > maxWidth) break;
        width += advance;
        offset = (size_t)(p - begin);
    }
    // `offset` counts the bytes that fit, so it equals value.size() when the whole
    // string fits. Callers use the result as "how much to drop from the front",
    // and dropping everything is the opposite of what a fitting string means -
    // that bug made a short path render as a bare "..." and an empty text field
    // render as nothing at all. Nothing cut is reported as 0.
    return offset < value.size() ? offset : 0;
}

} // namespace s3d
