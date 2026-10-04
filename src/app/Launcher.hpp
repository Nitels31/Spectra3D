#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "../core/Project.hpp"
#include "../i18n/Localizer.hpp"
#include "../platform/Filesystem.hpp"
#include "../platform/Window.hpp"
#include "../render/Camera.hpp"
#include "../render/UiRenderer.hpp"
#include "../ui/Theme.hpp"
#include "../ui/UiContext.hpp"

namespace s3d {

// Start menu. Owns the whole window while it is the active app state: no 3D
// viewport, no editor panels, all input.
//
// Layout is the one from the concept sketch: a tab strip across the top, a
// sidebar down the left, and one list of project rows filling the rest. Each
// row carries its own Open and Delete buttons - there is no shared "selected
// project" action bar, because the row you are looking at is the one you mean.
//
// Frame contract, mirroring Editor::renderUi():
//   * initialize() once, after UiRenderer::buildFonts()
//   * update() once per frame while the launcher is active. It calls
//     ctx.beginInput() itself, and ui.begin()/ui.end() itself, so the integrator
//     must not call either around it.
//   * the returned Result says what happened; OpenProject fills the path.
class Launcher {
public:
    enum class Result { Stay, OpenProject, Quit };

    // Which list the content area is showing. Projects is the default; Settings
    // and Account are sidebar destinations, not filters.
    enum class View { Projects, Favorites, Settings, Account, NewProject };

    explicit Launcher(i18n::Localizer& localizer);

    // Reads settings.ini, mirrors the values into the widgets, pushes the root
    // and the language into the library and the localizer, then scans.
    void initialize();

    // One frame: handles input and draws. Returns what the user chose.
    // `selectedProjectPath` is filled when the result is OpenProject.
    Result update(UiRenderer& ui, UiContext& ctx, const Window& window, int framebufferWidth,
                  int framebufferHeight, std::string& selectedProjectPath);

    ProjectLibrary& projects() { return projects_; }
    Settings& settings() { return settings_; }
    // Read-only view for the frame loop, which must not mutate the launcher.
    const Settings& settings() const { return settings_; }

    // Pushes settings (project root, language) into the library and the
    // localizer, rescans, and writes settings.ini back.
    void applySettings();

    // Optional content for the diagnostics line. Both arguments may be null;
    // the line then shows only the framebuffer size. Purely cosmetic - the
    // integrator can leave this alone entirely.
    void setGraphicsInfo(const char* renderer, const char* version);

    // Human readable reason for the last failed action, already localised.
    const std::string& lastError() const { return lastError_; }

private:
    // Widget ids. Disjoint from the editor's 100..900 block because both can be
    // live in the same UiContext instance.
    enum : int {
        kPointerClaim = 1000,   // "the pointer is over the launcher"
        kNameField = 1001,
        kCreateButton = 1002,
        kCancelCreateButton = 1003,
        kRootField = 1004,
        kBrowseButton = 1005,
        kApplyButton = 1006,
        kResetButton = 1007,
        kLangEnglishButton = 1008,
        kLangUkrainianButton = 1009,
        kRescanButton = 1010,
        kQuitButton = 1011,
        kPickerCancelButton = 1012,
        kDeleteConfirmButton = 1013,
        kDeleteCancelButton = 1014,
        kNewProjectButton = 1015,
        kThemeSelect = 1040,
        kFrameCapSelect = 1041,

        // Sidebar and tab strip are not clickable in the row sense, so they get
        // ids below kRowIdBase.
        kSidebarProjects = 1020,
        kSidebarAccount = 1021,
        kSidebarSettings = 1022,
        kTabAll = 1023,
        kTabFavorites = 1024,

        // Per-row ids. The row widgets of one project are addressed as
        // kRowIdBase + index * kRowIdStride + slot, so a row never collides with
        // another row's buttons.
        kRowIdBase = 1100,
        kRowIdStride = 8,
    };
    enum : int {
        kRowSlotOpen = 0,
        kRowSlotDelete = 1,
        kRowSlotFavorite = 2,
        kRowSlotRow = 3,
    };

    // Everything is derived from the framebuffer size once per frame, so a
    // window smaller than the design size just gets a smaller list.
    struct Layout {
        Rect topBar;
        Rect sidebar;
        Rect content;      // the list of project rows
        Rect footer;
        Rect newProject;   // the name card, centred in the content area
        Rect settings;     // the settings column
        Rect account;
    };

    // --- frame ---------------------------------------------------------------
    void recomputeLayout(int fbWidth, int fbHeight);
    void drawFrame(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth, int fbHeight);
    void drawTopBar(UiRenderer& ui, UiContext& ctx, int fbWidth, int fbHeight);
    void drawSidebar(UiRenderer& ui, UiContext& ctx, int fbWidth, int fbHeight);
    void drawProjectList(UiRenderer& ui, UiContext& ctx, int fbWidth);
    void drawEmpty(UiRenderer& ui, int fbWidth, const char* key, const char* hint);
    void drawNewProject(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth);
    void drawSettingsPanel(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth);
    void drawAccount(UiRenderer& ui, UiContext& ctx, int fbWidth);
    void drawFooter(UiRenderer& ui, UiContext& ctx, int fbWidth);
    void drawDeleteConfirm(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth,
                           int fbHeight);
    void drawFolderPicker(UiRenderer& ui, UiContext& ctx, Window& window, int fbWidth,
                          int fbHeight);
    void handleKeys(Window& window);

    // --- actions -------------------------------------------------------------
    void createProject();
    void requestOpen(const std::string& path);
    // Remembers the project, writes settings.ini and hands the path back.
    bool openProject(const std::string& path, std::string& out);
    void openPicker();
    // In-app folder browser, used only where the platform has no native dialog.
    void openFolderBrowser();
    void refreshPicker();
    void movePickerSelection(int delta);
    void activatePickerRow();
    void closePicker();
    void applyRoot(const std::string& root);
    void setLanguage(LanguageTag tag);
    void setFrameRateCap(int cap);
    void setTheme(theme::ThemeId id);
    void moveSelection(int delta);
    void clampSelection();
void toggleFavorite(int index);
    void askDelete(int row);
    void confirmDelete();
    void cancelDelete();
    // Switching view resets the list selection, because "index 2" means a
    // different project in every list.
// One row of the settings that picks one value out of several: a caption, the
    // current value, and - when opened - the list. Returns the newly chosen value,
    // or -1 when nothing changed.
    //
    // A list, not a row of buttons: four buttons means four measured widths to
    // keep in sync and a page that turns into a wall of controls, and the answer
    // to "which of these is it now?" is not visible without counting columns.
    struct SelectItem {
        const char* label;
        int value;
    };
    int drawSelect(UiRenderer& ui, UiContext& ctx, const Rect& rect, const char* caption,
                   const SelectItem* items, int count, int current, int id, int fbWidth);

    void setView(View view);
    const char* nameHint() const;

    // --- list model ----------------------------------------------------------
    // Indexes into projects().projects(). `visible_` is that vector filtered by
    // the active tab, so every id and every click maps back to one source of
    // truth and a starred project cannot disagree between the two tabs.
    void rebuildVisible();
    int selectedProject() const;
    int listRows() const;
    int maxListScroll() const;

    // --- drawing helpers -----------------------------------------------------
    const char* text(const char* key) const { return loc_.get(key); }

    // Never lets a string escape the framebuffer horizontally. Text is shrunk to
    // fit before a single character is dropped, because "C:/.../MyProject" and
    // "..." are not equally useful.
    void drawText(UiRenderer& ui, float x, float y, const char* value, const Color& color,
                  int fbWidth) const;
    // Same, for a slot of a known width. `keepTail` drops characters from the
    // front instead of the back, which is what a path needs.
    void drawTextFit(UiRenderer& ui, float x, float y, float maxWidth, bool keepTail,
                     const std::string& value, const Color& color, int fbWidth) const;
    // Font factor that makes `value` fit `maxWidth`, clamped to 1.0.
    float fitScale(UiRenderer& ui, const std::string& value, float maxWidth) const;
    void drawTextRight(UiRenderer& ui, float right, float y, const char* value,
                       const Color& color, int fbWidth) const;
    void drawTextCentered(UiRenderer& ui, const Rect& rect, float y, const char* value,
                          const Color& color) const;
    void drawRow(UiRenderer& ui, const Rect& rect, bool selected, bool hovered,
                 const char* name, const char* info, int fbWidth) const;
    void drawField(UiRenderer& ui, const Rect& rect, const TextField& field,
                   const char* placeholder, int fbWidth) const;
    void drawFieldValue(UiRenderer& ui, const Rect& rect, const char* value, bool editing,
                        bool asPath, int fbWidth) const;
    void drawDisabledButton(UiRenderer& ui, UiContext& ctx, int id, const Rect& rect,
                            const char* label, int fbWidth) const;
    // One line of the settings/account column: a dim caption over a value.
    void drawFieldRow(UiRenderer& ui, int fbWidth, float& y, const char* caption,
                      const char* value, const Color& valueColor);
    // The small square project mark drawn at the left of every row.
    void drawProjectIcon(UiRenderer& ui, const Rect& rect, bool selected, bool favorite) const;
    void drawStar(UiRenderer& ui, UiContext& ctx, int id, const Rect& rect, bool on,
                    const Color& background) const;
    // Width a label needs for a button, measured. Never a guessed constant.
    float buttonWidthFor(UiRenderer& ui, const char* label, float minimum) const;
    // The gear in the sidebar corner: a ring plus four bars, because the batcher
    // has no rotated primitives.
    void drawGear(UiRenderer& ui, const Rect& rect, bool active, bool hovered) const;
    // Caption of the content section header, which follows the active tab.
    const char* sectionTitle() const;
    // Draws the section header plus the body panel, and hands back the inner
    // content rectangle. `trailing` is right-aligned in the header strip.
    Rect section(UiRenderer& ui, UiContext& ctx, const Rect& area, const char* title,
                 const char* trailing, int fbWidth, float& contentY) const;
    // Byte offset of the suffix of `value` that fits in `maxWidth`: long values
    // are cut from the left so the last path component, the part that
    // identifies a folder, stays on screen.
    size_t fitOffset(UiRenderer& ui, const std::string& value, float maxWidth) const;

    i18n::Localizer& loc_;
    ProjectLibrary projects_;
    Settings settings_;

    TextField nameField_;
    TextField rootField_;
    std::string rootDraft_;      // committed or edited value of the root field
    std::string lastError_;

    View view_ = View::Projects;
    // Id of the select whose list is open, or -1. Only one list at a time: two open
    // at once overlap, and the second one covers the first one's rows.
    int openSelect_ = -1;
    std::vector<int> visible_;  // indexes into projects_.projects()
    int selected_ = -1;         // index into visible_
    int listScroll_ = 0;        // first visible row
    bool requestedOpen_ = false;
    std::string requestedPath_;
    bool quitRequested_ = false;

    // Delete confirmation modal. `pendingDelete_` is an index into visible_, and
    // -1 means no modal: nothing is ever removed without the modal being open
    // and Ok being pressed.
    bool deleteOpen_ = false;
    int pendingDelete_ = -1;

    // Folder picker modal.
    bool pickerOpen_ = false;
    std::string pickerPath_;
    std::string pickerMessage_;
    std::vector<fs::DirEntry> pickerEntries_;
    int pickerSelected_ = 0;

    std::string graphicsRenderer_;
    std::string graphicsVersion_;
    std::string versionLabel_;
    std::string diagnostics_;   // cached; rebuilt when the GL info or size changes
    bool diagnosticsDirty_ = true;
    int diagnosticsWidth_ = 0;
    int diagnosticsHeight_ = 0;

    Layout layout_;

    // Right edge that drawText clips to. Each section sets it so a long
    // string cannot bleed into the neighbouring column.
    mutable float textClipRight_ = -1.0f;
};

} // namespace s3d