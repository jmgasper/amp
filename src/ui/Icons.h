// The glyphs the buttons draw. The shapes come from the bundled Font Awesome 6
// Free font, which is registered with the font server the first time the app runs.
#pragma once
#include <GraphicsDefs.h>
#include <Rect.h>

class BView;

namespace amp {
namespace icons {

enum Icon {
    kPlay,
    kPause,
    kPrevious,
    kNext,
    kShuffle,
    kRepeat,
    kPlus,
    kGear,
    kList,
    kAlbumList,
    kGrid,
    kVolume,
    kMusic,
    kUser,
    kAlbum,
    kStream,
    kClose,
    kCheck,
    kWarning,
};

// Makes the Font Awesome font available (copying it into the user font
// directory on first run) and loads it. Returns whether glyphs can be drawn.
bool Init();
bool Available();

// Draws the glyph centred on its ink inside `rect`, with the font's em size set
// to `size`. The glyph keeps its designed proportions.
void Draw(BView* view, Icon icon, BRect rect, float size, rgb_color color);

// Draws the glyph centred inside `rect` (inset by `margin`), sized to fill it.
void DrawFitted(BView* view, Icon icon, BRect rect, rgb_color color, float margin = 0.0f);

} // namespace icons
} // namespace amp
