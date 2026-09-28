#include "Icons.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Path.h>
#include <View.h>
#include <OS.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace amp {
namespace icons {

namespace {

// Font Awesome 6 Free, SIL OFL 1.1. The file is shipped in the app's package
// under data/Amp and copied into the user font directory on first use, which is
// the one location the font server rescans on request.
const char* kFontFileName = "FontAwesome6Free-Solid-900.otf";
const char* kFamily = "Font Awesome 6 Free";
const char* kStyle = "Solid";

// Font Awesome 6 Free Solid glyphs (UTF-8).
const char* const kGlyphs[] = {
    "\xef\x81\x8b", // kPlay        U+F04B
    "\xef\x81\x8c", // kPause       U+F04C
    "\xef\x81\x88", // kPrevious    U+F048 backward-step
    "\xef\x81\x91", // kNext        U+F051 forward-step
    "\xef\x81\xb4", // kShuffle     U+F074
    "\xef\x8d\xa3", // kRepeat      U+F363
    "+",            // kPlus        U+002B
    "\xef\x80\x93", // kGear        U+F013
    "\xef\x80\xba", // kList        U+F03A
    "\xef\x80\xa2", // kAlbumList   U+F022 rectangle-list
    "\xef\x80\x8a", // kGrid        U+F00A table-cells
    "\xef\x80\xa8", // kVolume      U+F028 volume-high
    "\xef\x80\x81", // kMusic       U+F001
    "\xef\x80\x87", // kUser        U+F007
    "\xef\x94\x9f", // kAlbum       U+F51F compact-disc
    "\xef\x94\x99", // kStream      U+F519 tower-broadcast
    "\xef\x80\x8d", // kClose       U+F00D xmark
    "\xef\x80\x8c", // kCheck       U+F00C check
    "\xef\x81\xb1", // kWarning     U+F071 triangle-exclamation
    "\xef\x80\xa3", // kLock        U+F023 lock
};

int fState = 0; // 0 unknown, 1 ready, -1 unavailable

// Copies the font shipped inside the app's package next to the user's other
// fonts; the font server watches that directory and picks the file up.
bool InstallBundledFont(const char* destination)
{
    BPath source;
    if (find_directory(B_SYSTEM_DATA_DIRECTORY, &source) != B_OK)
        return false;
    source.Append("Amp");
    source.Append(kFontFileName);
    BFile in(source.Path(), B_READ_ONLY);
    if (in.InitCheck() != B_OK)
        return false;
    BFile out(destination, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (out.InitCheck() != B_OK)
        return false;
    char buffer[64 * 1024];
    while (true) {
        ssize_t read = in.Read(buffer, sizeof(buffer));
        if (read <= 0)
            return read == 0;
        if (out.Write(buffer, read) != read)
            return false;
    }
}

} // namespace

bool Init()
{
    if (fState != 0)
        return fState > 0;

    BPath fonts;
    if (find_directory(B_USER_NONPACKAGED_FONTS_DIRECTORY, &fonts, true) == B_OK) {
        create_directory(fonts.Path(), 0755);
        BPath destination(fonts);
        destination.Append(kFontFileName);
        BEntry entry(destination.Path());
        if (!entry.Exists())
            InstallBundledFont(destination.Path());
    }

    // the server rescans on request; give it a moment to publish the family
    for (int attempt = 0; attempt < 25; attempt++) {
        update_font_families(false);
        BFont probe;
        if (probe.SetFamilyAndStyle(kFamily, kStyle) == B_OK) {
            fState = 1;
            return true;
        }
        snooze(20000);
    }
    fprintf(stderr, "Amp: the Font Awesome icon font is not available\n");
    fState = -1;
    return false;
}

bool Available()
{
    return Init();
}

void Draw(BView* view, Icon icon, BRect rect, float size, rgb_color color)
{
    if (view == nullptr || !Init())
        return;
    if (icon < kPlay || icon > kLock)
        return;

    BFont font;
    font.SetFamilyAndStyle(kFamily, kStyle);
    font.SetSize(size);

    const char* glyph = kGlyphs[icon];
    BRect box;
    font.GetBoundingBoxesAsGlyphs(glyph, 1, B_SCREEN_METRIC, &box);

    // centre the ink, not the em square: Font Awesome glyphs sit on the baseline
    // with different amounts of space above and below
    BPoint pen(rect.left + (rect.Width() - box.Width()) / 2 - box.left,
        rect.top + (rect.Height() - box.Height()) / 2 - box.top);

    BFont previous;
    view->GetFont(&previous);
    drawing_mode mode = view->DrawingMode();
    view->SetFont(&font);
    view->SetHighColor(color);
    view->SetDrawingMode(B_OP_OVER);
    view->DrawString(glyph, pen);
    view->SetDrawingMode(mode);
    view->SetFont(&previous);
}

void DrawFitted(BView* view, Icon icon, BRect rect, rgb_color color, float margin)
{
    float size = rect.Height() - margin * 2;
    if (size <= 0)
        return;
    Draw(view, icon, rect, size, color);
}

} // namespace icons
} // namespace amp
