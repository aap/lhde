// The Explorer's views in the Longhorn hosts (the explorer's Longhorn frame and
// the Longhorn file dialog), after build 4074: three views, each with an image
// size, and what they remember. No win32 here: browser.cpp puts a View on the
// list view, the controls on top of it (4074's view button and slider, the
// keys, the menus) only choose Views through this model.
//
//   Details  16..32 px   the table; the size is the rows' image
//   Tiles    32..96 px   image left, name and (from 48 px) two grey lines right
//   Icons    16..256 px  image above, name below (or no name: Hide File Names)
//
// 4074's slider (System.Windows.Explorer ViewModeSlider, the viewpopup XAML) has
// five stops, top to bottom: Details 16->32 and Tiles 32->96 growing downward,
// then Large 256->128, Medium 128->48 and Small 48->16 shrinking downward (the
// size jumps from 96 to 256 between Tiles and Large). Each stop remembers the
// size it was last shown at; among the three icon stops the one used last is
// what "Icons" means (the View button's cycle, Ctrl+3).
#pragma once
#include <string>
#include <vector>

namespace shell {
namespace viewmodel {

enum class Mode { Details, Tiles, Icons };

struct View {
    Mode mode = Mode::Icons;
    int size = 48;
    bool hide_names = false;     // Icons only; kept (and inert) in the other two
    bool operator==(const View& o) const { return mode == o.mode && size == o.size && hide_names == o.hide_names; }
    bool operator!=(const View& o) const { return !(*this == o); }
};

int min_size(Mode m);
int max_size(Mode m);
View clamped(View v);
const char* mode_name(Mode m);            // "details", "tiles", "icons"
// names as the views file and filedlg_view keep them: "details@20", "tiles@48",
// "icons@96", "icons@96+nonames". parse also reads the earlier names (thumbnails,
// smallicons, list, "+thumbnails"): Thumbnails and Small Icons are Icons, List is
// Details. *sized: whether the text gave a size (else v.size is the mode's start).
std::string format(const View& v);
bool parse(const std::string& text, View& v, bool* sized = nullptr);

// ---- 4074's slider ----------------------------------------------------------
enum { STOPS = 5, STOP_DETAILS = 0, STOP_TILES, STOP_LARGE, STOP_MEDIUM, STOP_SMALL };
struct Stop {
    Mode mode;
    int min, max;
    bool ascending;              // the size grows downward (Details, Tiles)
    int start;                   // the size before the user chose one
    const char* name;            // "Details", "Tiles", "Large Icons", "Medium Icons", "Small Icons"
};
const Stop& stop(int i);
int stop_of(const View& v);              // the first stop whose mode and range hold the view's size
// ViewModeButton.ImageSizeFromPosY / PosYFromImageSize: linear over the stop's span of
// h pixels (y from its top), mirrored for the shrinking stops
int size_at(int i, int y, int h);
int pos_of(int i, int size, int h);

// the zoom steps of a view's mode (Ctrl+wheel, Ctrl+plus/minus): 4051's levels
// (16 21 27 36 48 64 84 111 147 194 256) inside the mode's range, its ends included
std::vector<int> levels(Mode m);
View step(const View& v, int dir);       // +1 bigger, -1 smaller; the same view at the end

// ---- what the views remember -----------------------------------------------
struct Memory {
    int size[STOPS];
    int icons = STOP_MEDIUM;              // the icon stop used last (4074's ModeDefault)
    Memory();
    // a view was shown: its stop keeps the size and, an icon stop, becomes "Icons"
    // (4074 updated a stop only when the slider was dragged over it; here every way
    // of showing a view counts, so Icons comes back at the size last seen)
    void shown(const View& v);
    View recall(Mode m) const;            // the mode at the size it remembers
    View at(int i) const;                 // stop i at the size it remembers
    // the View button: Details -> Tiles -> Icons -> Details, each as remembered
    View next(const View& v) const;
    std::string save() const;             // "details=16 tiles=48 large=128 medium=48 small=16 icons=medium"
    void load(const std::string& text);
};

} // namespace viewmodel
} // namespace shell
