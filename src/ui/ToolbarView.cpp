#include "ToolbarView.h"
#include "App.h"
#include "Icons.h"
#include "Theme.h"
#include "core/Model.h"
#include <Bitmap.h>
#include <Font.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Region.h>
#include <Window.h>
#include <cmath>
#include <cstdio>

namespace amp {

namespace {
const uint32 kMsgPulse = 'tbpl';
// the left end: transport buttons, then the volume slider between its two speakers
const float kVolumeGap = 32;         // right edge of Next to the slider (the small speaker sits between)
const float kSliderWidth = 90;       // preferred; narrower windows shrink it down to kSliderMinWidth
const float kSliderMinWidth = 56;
const float kSpeakerRoom = 26;       // the loud speaker right of the slider
const float kLcdMinWidth = 220;
const float kLcdComfortWidth = 300;  // below this the slider gives up width first
const float kLcdMaxWidth = 560;
const float kGap = 24;               // between the display, the view buttons and the search field
const float kSearchMinWidth = 120;
const float kViewWidth = 96;
// the narrowest toolbar that still has room for all of it
const float kMinWidth = 126 + kVolumeGap + kSliderMinWidth + kSpeakerRoom + 16 + kLcdMinWidth + kGap + kViewWidth
    + kGap + kSearchMinWidth + 14;
}

ToolbarView::ToolbarView()
    : BView("toolbar", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(kMinWidth, theme::kToolbarHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, theme::kToolbarHeight));
    SetExplicitPreferredSize(BSize(1000, theme::kToolbarHeight));
    fSearch = new BTextControl("search", nullptr, "", new BMessage(kMsgSearch));
    fSearch->SetModificationMessage(new BMessage(kMsgSearch));
    AddChild(fSearch);
}

ToolbarView::~ToolbarView()
{
    delete fPulse;
}

void ToolbarView::DetachedFromWindow()
{
    delete fPulse;
    fPulse = nullptr;
    BView::DetachedFromWindow();
}

void ToolbarView::SetDeviceStatus(const DeviceStatus& status)
{
    fDevice = status;
    // an indeterminate bar animates; nothing else needs a timer
    bool animate = status.active && status.fraction < 0 && !status.done;
    if (animate && !fPulse && Window()) {
        BMessage pulse(kMsgPulse);
        fPulse = new BMessageRunner(BMessenger(this), &pulse, 60000);
    } else if (!animate) {
        delete fPulse;
        fPulse = nullptr;
    }
    Invalidate(fLcdRect);
}

void ToolbarView::MessageReceived(BMessage* message)
{
    if (message->what == kMsgPulse) {
        fStripePhase = fmodf(fStripePhase + 1.0f, 12.0f);
        Invalidate(fProgressRect.InsetByCopy(-2, -2));
        return;
    }
    BView::MessageReceived(message);
}

BRect ToolbarView::CancelRect() const
{
    return BRect(fLcdRect.right - 24, fLcdRect.top + 7, fLcdRect.right - 10, fLcdRect.top + 21);
}

void ToolbarView::DrawDeviceStatus(BRect r)
{
    DrawMiniDisc(this, fArtRect.InsetByCopy(2, 2), Rgb(62, 84, 122), Rgb(176, 182, 190), true);
    float margin = fArtRect.Width() + 16;
    BRect textArea(r.left + margin, r.top + 4, r.right - margin, r.top + 20);
    BFont bold(be_bold_font);
    bold.SetSize(12);
    BFont plain(be_plain_font);
    plain.SetSize(11);
    SetFont(&bold);
    SetHighColor(theme::kLcdText);
    DrawTruncated(this, fDevice.headline.String(), textArea, B_ALIGN_CENTER, 0);
    SetFont(&plain);
    SetHighColor(theme::kLcdSecondary);
    DrawTruncated(this, fDevice.detail.String(), BRect(textArea.left, r.top + 19, textArea.right, r.top + 34), B_ALIGN_CENTER, 0);

    // the bar: filled to the fraction, or moving stripes while the length is unknown
    BRect bar = fProgressRect;
    SetHighColor(theme::kLcdBarBackground);
    FillRoundRect(bar, 2.5f, 2.5f);
    if (fDevice.fraction >= 0 || fDevice.done) {
        float fraction = fDevice.done ? 1.0f : std::min(1.0f, fDevice.fraction);
        float right = floorf(bar.left + bar.Width() * fraction);
        if (right > bar.left + 1)
            FillRoundGradient(this, BRect(bar.left, bar.top, right, bar.bottom), 2.5f,
                Blend(theme::kLcdBar, Rgb(255, 255, 255), 0.12f), theme::kLcdBar);
    } else {
        BRegion clip(bar.InsetByCopy(1, 0));
        ConstrainClippingRegion(&clip);
        SetHighColor(theme::kLcdBar);
        for (float x = bar.left - 12 + fStripePhase; x < bar.right + 12; x += 12) {
            BPoint stripe[4] = {BPoint(x, bar.bottom), BPoint(x + 5, bar.top), BPoint(x + 10, bar.top), BPoint(x + 5, bar.bottom)};
            FillPolygon(stripe, 4);
        }
        ConstrainClippingRegion(nullptr);
    }
    SetHighColor(Blend(theme::kLcdBorder, Rgb(0, 0, 0), 0.1f));
    StrokeRoundRect(bar, 2.5f, 2.5f);
    BFont small(be_plain_font);
    small.SetSize(9);
    SetFont(&small);
    SetHighColor(theme::kLcdText);
    DrawTruncated(this, fDevice.leftLabel.String(), BRect(fArtRect.right + 4, bar.top - 5, bar.left - 7, bar.bottom + 5), B_ALIGN_RIGHT, 0);
    DrawTruncated(this, fDevice.rightLabel.String(), BRect(bar.right + 7, bar.top - 5, r.right - 4, bar.bottom + 5), B_ALIGN_LEFT, 0);

    // right-hand corner: cancel while running, a check mark or a warning once finished
    BRect corner = CancelRect();
    if (fDevice.cancellable) {
        bool pressed = fTracking && fPressed == kCancelDevice;
        SetHighColor(pressed ? Rgb(80, 88, 76) : Rgb(128, 136, 122));
        FillEllipse(corner);
        icons::Draw(this, icons::kClose, corner, 8.5f, Rgb(236, 240, 230));
    } else if (fDevice.done)
        icons::Draw(this, fDevice.failed ? icons::kWarning : icons::kCheck, corner, 11,
            fDevice.failed ? Rgb(170, 90, 40) : theme::kLcdSecondary);
}

void ToolbarView::AttachedToWindow()
{
    fSearch->SetTarget(Window());
    Layout();
}

void ToolbarView::FrameResized(float width, float height)
{
    Layout();
    Invalidate();
}

void ToolbarView::Layout()
{
    BRect bounds = Bounds();
    float cy = 28;
    fPrevRect = BRect(18, cy - 13, 44, cy + 13);
    fPlayRect = BRect(54, cy - 18, 90, cy + 18);
    fNextRect = BRect(100, cy - 13, 126, cy + 13);
    // right end: search field and view buttons
    float searchWidth = std::min(180.0f, std::max(kSearchMinWidth, bounds.Width() * 0.15f));
    fSearchRect = BRect(bounds.right - searchWidth - 14, 14, bounds.right - 14, 38);
    fViewRect = BRect(fSearchRect.left - kViewWidth - kGap, 16, fSearchRect.left - kGap, 36);
    // the volume slider sits clear of the Next button (its small speaker in between) and
    // gives up width when the display would otherwise get cramped
    float sliderLeft = fNextRect.right + kVolumeGap;
    float lcdRight = fViewRect.left - kGap;
    float room = lcdRight - (sliderLeft + kSliderWidth + kSpeakerRoom + 16);
    float slider = kSliderWidth;
    if (room < kLcdComfortWidth)
        slider = std::max(kSliderMinWidth, kSliderWidth - (kLcdComfortWidth - room));
    fVolumeRect = BRect(sliderLeft, cy - 8, sliderLeft + slider, cy + 8);
    float lcdLeft = fVolumeRect.right + kSpeakerRoom + 16;
    float lcdWidth = std::max(kLcdMinWidth, std::min(kLcdMaxWidth, lcdRight - lcdLeft));
    float lcdCenter = floorf((lcdLeft + lcdRight) / 2);
    if (lcdRight - lcdLeft < kLcdMinWidth)
        lcdCenter = floorf(lcdLeft + kLcdMinWidth / 2); // too narrow: never over the volume
    fLcdRect = BRect(lcdCenter - floorf(lcdWidth / 2), 5, lcdCenter + floorf(lcdWidth / 2), 58);
    // artwork square on the left, progress row along the bottom
    fArtRect = BRect(fLcdRect.left + 5, fLcdRect.top + 5, fLcdRect.left + 5 + 43, fLcdRect.top + 5 + 43);
    float barTop = fLcdRect.bottom - 13;
    fProgressRect = BRect(fArtRect.right + 44, barTop, fLcdRect.right - 46, barTop + 4);
    fSearch->MoveTo(fSearchRect.LeftTop());
    fSearch->ResizeTo(fSearchRect.Width(), fSearchRect.Height());
}

void ToolbarView::SetPlayerState(PlayerState state)
{
    fState = state;
    Invalidate();
}

void ToolbarView::SetTrackInfo(const BString& title, const BString& artist, const BString& album, bool isMA)
{
    fTitle = title;
    fArtist = artist;
    fAlbum = album;
    fIsMA = isMA;
    Invalidate(fLcdRect);
}

void ToolbarView::SetArtwork(const ArtRequest& request)
{
    fArt = request;
    Invalidate(fLcdRect);
}

void ToolbarView::SetQuality(const BString& quality)
{
    if (quality == fQuality)
        return;
    fQuality = quality;
    Invalidate(fLcdRect);
}

void ToolbarView::SetLoadingText(const BString& text)
{
    fLoading = text;
    Invalidate(fLcdRect);
}

void ToolbarView::SetProgress(int64_t positionMs, int64_t durationMs)
{
    if (fTracking && fPressed == kProgress)
        return;
    fPosition = positionMs;
    fDuration = durationMs;
    Invalidate(fLcdRect);
}

void ToolbarView::SetVolume(float volume)
{
    fVolume = volume;
    Invalidate(fVolumeRect.InsetByCopy(-20, -6));
}

void ToolbarView::SetViewMode(int mode)
{
    fViewMode = mode;
    Invalidate(fViewRect.InsetByCopy(-2, -16));
}

void ToolbarView::DrawRoundButton(BRect rect, Hot which)
{
    bool pressed = fPressed == which && fTracking;
    rgb_color top = pressed ? Rgb(190, 190, 190) : Rgb(252, 252, 252);
    rgb_color bottom = pressed ? Rgb(150, 150, 150) : Rgb(196, 196, 196);
    // shadow
    SetHighColor(0, 0, 0, 40);
    SetDrawingMode(B_OP_ALPHA);
    FillEllipse(rect.OffsetByCopy(0, 1.5));
    SetDrawingMode(B_OP_COPY);
    BGradientLinear gradient(BPoint(rect.left, rect.top), BPoint(rect.left, rect.bottom));
    gradient.AddColor(top, 0);
    gradient.AddColor(bottom, 255);
    FillEllipse(rect, gradient);
    SetHighColor(118, 118, 118);
    SetPenSize(1);
    StrokeEllipse(rect);
    // glyph: Font Awesome, centred on its ink
    icons::Icon glyph = icons::kNext;
    if (which == kPlay)
        glyph = fState == kPlaying ? icons::kPause : icons::kPlay;
    else if (which == kPrev)
        glyph = icons::kPrevious;
    // the play circle is larger than the two step buttons: keep the ink at the
    // same fraction of each circle so the three glyphs look equally weighted
    float size = rect.Height() * (which == kPlay ? 0.42f : 0.50f);
    icons::Draw(this, glyph, rect, size, Rgb(60, 60, 60));
}

void ToolbarView::DrawVolume()
{
    BRect r = fVolumeRect;
    float cy = (r.top + r.bottom) / 2;
    // a speaker either side of the slider: the far one carries the "louder" idea
    icons::Draw(this, icons::kVolume, BRect(r.left - 22, cy - 8, r.left - 8, cy + 8), 11, Rgb(90, 90, 90));
    icons::Draw(this, icons::kVolume, BRect(r.right + 8, cy - 9, r.right + kSpeakerRoom, cy + 9), 13, Rgb(90, 90, 90));
    // track
    BRect track(r.left, cy - 2, r.right, cy + 2);
    SetHighColor(0, 0, 0, 50);
    SetDrawingMode(B_OP_ALPHA);
    FillRoundRect(track.OffsetByCopy(0, 1), 2, 2);
    SetDrawingMode(B_OP_COPY);
    FillRoundGradient(this, track, 2, Rgb(150, 150, 150), Rgb(190, 190, 190));
    float knobX = r.left + fVolume * r.Width();
    BRect filled(r.left, cy - 2, knobX, cy + 2);
    FillRoundGradient(this, filled, 2, Rgb(96, 96, 96), Rgb(130, 130, 130));
    SetHighColor(110, 110, 110);
    StrokeRoundRect(track, 2, 2);
    BRect knob(knobX - 6, cy - 6, knobX + 6, cy + 6);
    SetHighColor(0, 0, 0, 60);
    SetDrawingMode(B_OP_ALPHA);
    FillEllipse(knob.OffsetByCopy(0, 1));
    SetDrawingMode(B_OP_COPY);
    BGradientLinear gradient(knob.LeftTop(), knob.LeftBottom());
    gradient.AddColor(Rgb(250, 250, 250), 0);
    gradient.AddColor(Rgb(190, 190, 190), 255);
    FillEllipse(knob, gradient);
    SetHighColor(100, 100, 100);
    StrokeEllipse(knob);
}

void ToolbarView::DrawProgress(BRect lcd)
{
    // a slim groove with the elapsed part filled and a small round playhead
    BRect bar = fProgressRect;
    SetHighColor(theme::kLcdBarBackground);
    FillRoundRect(bar, 2.5f, 2.5f);
    SetHighColor(Blend(theme::kLcdBarBackground, Rgb(0, 0, 0), 0.22f));
    StrokeLine(BPoint(bar.left + 2, bar.top), BPoint(bar.right - 2, bar.top)); // inner shadow
    float fraction = fDuration > 0 ? std::min(1.0f, std::max(0.0f, (float)fPosition / (float)fDuration)) : 0.0f;
    float knobX = floorf(bar.left + 5 + (bar.Width() - 10) * fraction);
    if (fDuration > 0 && knobX > bar.left + 2) {
        BRect done(bar.left, bar.top, knobX, bar.bottom);
        FillRoundGradient(this, done, 2.5f, Blend(theme::kLcdBar, Rgb(255, 255, 255), 0.12f), theme::kLcdBar);
    }
    SetHighColor(Blend(theme::kLcdBorder, Rgb(0, 0, 0), 0.1f));
    StrokeRoundRect(bar, 2.5f, 2.5f);
    if (fDuration > 0) {
        float cy = bar.top + 2; // the groove is five pixels tall: this is its middle row
        BRect knob(knobX - 5, cy - 5, knobX + 5, cy + 5);
        SetDrawingMode(B_OP_ALPHA);
        SetHighColor(0, 0, 0, 55);
        FillEllipse(knob.OffsetByCopy(0, 1));
        SetDrawingMode(B_OP_COPY);
        BGradientLinear gradient(knob.LeftTop(), knob.LeftBottom());
        gradient.AddColor(Rgb(255, 255, 255), 0);
        gradient.AddColor(Rgb(206, 212, 200), 255);
        FillEllipse(knob, gradient);
        SetHighColor(Blend(theme::kLcdText, theme::kLcdBorder, 0.45f));
        StrokeEllipse(knob);
    }
    BFont small(be_plain_font);
    small.SetSize(9);
    SetFont(&small);
    SetHighColor(theme::kLcdText);
    BString elapsed(FormatDuration(fPosition).c_str());
    if (elapsed.IsEmpty())
        elapsed = "0:00";
    BString remaining("-");
    remaining << (fDuration > fPosition ? FormatDuration(fDuration - fPosition).c_str() : "0:00");
    DrawTruncated(this, elapsed.String(), BRect(fArtRect.right + 4, bar.top - 5, bar.left - 7, bar.bottom + 5), B_ALIGN_RIGHT, 0);
    DrawTruncated(this, remaining.String(), BRect(bar.right + 7, bar.top - 5, lcd.right - 4, bar.bottom + 5), B_ALIGN_LEFT, 0);
}

void ToolbarView::DrawLcd()
{
    BRect r = fLcdRect;
    SetHighColor(0, 0, 0, 40);
    SetDrawingMode(B_OP_ALPHA);
    FillRoundRect(r.OffsetByCopy(0, 1), 6, 6);
    SetDrawingMode(B_OP_COPY);
    FillRoundGradient(this, r, 6, theme::kLcdTop, theme::kLcdBottom);
    SetHighColor(theme::kLcdBorder);
    StrokeRoundRect(r, 6, 6);
    SetHighColor(255, 255, 255, 120);
    SetDrawingMode(B_OP_ALPHA);
    StrokeLine(BPoint(r.left + 6, r.top + 1), BPoint(r.right - 6, r.top + 1));
    SetDrawingMode(B_OP_COPY);

    if (fDevice.active) {
        DrawDeviceStatus(r);
        return;
    }
    BFont bold(be_bold_font);
    bold.SetSize(12);
    BFont plain(be_plain_font);
    plain.SetSize(11);
    if (fState == kStopped || fTitle.IsEmpty()) {
        // idle: the Amp mark
        float cx = r.left + r.Width() / 2;
        icons::Draw(this, icons::kMusic, BRect(cx - 40, r.top + 12, cx - 12, r.top + 40), 20, theme::kLcdSecondary);
        bold.SetSize(17);
        SetFont(&bold);
        SetHighColor(theme::kLcdSecondary);
        DrawString("Amp", BPoint(cx - 6, r.top + 33));
        return;
    }
    // artwork of the playing track
    BBitmap* art = fArt.key.empty() ? nullptr : App()->Art().Get(fArt.key, 88, &fArt);
    SetDrawingMode(B_OP_ALPHA);
    SetHighColor(0, 0, 0, 45);
    FillRect(fArtRect.OffsetByCopy(1, 2));
    SetDrawingMode(B_OP_COPY);
    DrawBitmapFitted(this, art, fArtRect);
    SetHighColor(Blend(theme::kLcdBorder, Rgb(0, 0, 0), 0.25f));
    StrokeRect(fArtRect);

    // right-hand column: quality label above the MA badge; the text stays centred between
    // two equal margins so long titles never run under either side
    float margin = fArtRect.Width() + 16;
    BFont tiny(be_bold_font);
    tiny.SetSize(8.5f);
    float rightEdge = r.right - 7;
    if (!fQuality.IsEmpty()) {
        SetFont(&tiny);
        float width = StringWidth(fQuality.String()) + 10;
        BRect pill(rightEdge - width, r.top + 6, rightEdge, r.top + 19);
        SetHighColor(Blend(theme::kLcdBottom, Rgb(255, 255, 255), 0.35f));
        FillRoundRect(pill, 4, 4);
        SetHighColor(theme::kLcdSecondary);
        StrokeRoundRect(pill, 4, 4);
        SetHighColor(theme::kLcdText);
        DrawTruncated(this, fQuality.String(), pill, B_ALIGN_CENTER, 0);
        margin = std::max(margin, width + 12);
    }
    if (fIsMA)
        DrawMABadge(this, BPoint(rightEdge - 22, r.top + (fQuality.IsEmpty() ? 7 : 23)), 11);

    BRect textArea(r.left + margin, r.top + 4, r.right - margin, r.top + 20);
    SetHighColor(theme::kLcdText);
    SetFont(&bold);
    DrawTruncated(this, fTitle.String(), textArea, B_ALIGN_CENTER, 0);
    SetFont(&plain);
    SetHighColor(theme::kLcdSecondary);
    BString line = fArtist;
    if (!fAlbum.IsEmpty()) {
        if (!line.IsEmpty())
            line << "  —  ";
        line << fAlbum;
    }
    if (fState == kLoading)
        line = fLoading.IsEmpty() ? "Loading…" : fLoading;
    DrawTruncated(this, line.String(), BRect(textArea.left, r.top + 19, textArea.right, r.top + 34), B_ALIGN_CENTER, 0);
    DrawProgress(r);
}

void ToolbarView::DrawViewButtons()
{
    BRect r = fViewRect;
    float w = r.Width() / 3;
    for (int i = 0; i < 3; i++) {
        BRect cell(r.left + i * w, r.top, r.left + (i + 1) * w, r.bottom);
        bool active = fViewMode == i;
        if (active)
            FillVerticalGradient(this, cell, Rgb(120, 120, 120), Rgb(150, 150, 150));
        else
            FillVerticalGradient(this, cell, Rgb(250, 250, 250), Rgb(205, 205, 205));
        SetHighColor(110, 110, 110);
        StrokeRect(cell);
        icons::Icon glyph = i == 0 ? icons::kList : (i == 1 ? icons::kAlbumList : icons::kGrid);
        icons::Draw(this, glyph, cell, cell.Height() * 0.55f, active ? Rgb(255, 255, 255) : Rgb(70, 70, 70));
    }
    BFont small(be_plain_font);
    small.SetSize(9);
    SetFont(&small);
    SetHighColor(theme::kToolbarText);
    DrawTruncated(this, "View", BRect(r.left, r.bottom + 2, r.right, r.bottom + 14), B_ALIGN_CENTER, 0);
    DrawTruncated(this, "Search", BRect(fSearchRect.left, fSearchRect.bottom + 2, fSearchRect.right, fSearchRect.bottom + 14), B_ALIGN_CENTER, 0);
}

void ToolbarView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    FillVerticalGradient(this, bounds, theme::kToolbarTop, theme::kToolbarBottom);
    SetHighColor(theme::kToolbarBorder);
    StrokeLine(bounds.LeftBottom(), bounds.RightBottom());
    SetHighColor(255, 255, 255, 140);
    SetDrawingMode(B_OP_ALPHA);
    StrokeLine(bounds.LeftTop(), bounds.RightTop());
    SetDrawingMode(B_OP_COPY);
    DrawRoundButton(fPrevRect, kPrev);
    DrawRoundButton(fPlayRect, kPlay);
    DrawRoundButton(fNextRect, kNext);
    DrawVolume();
    DrawLcd();
    DrawViewButtons();
}

ToolbarView::Hot ToolbarView::HitTest(BPoint where) const
{
    if (fPrevRect.Contains(where))
        return kPrev;
    if (fPlayRect.Contains(where))
        return kPlay;
    if (fNextRect.Contains(where))
        return kNext;
    if (fVolumeRect.InsetByCopy(-8, -6).Contains(where))
        return kVolume;
    if (fDevice.active) {
        if (fDevice.cancellable && CancelRect().InsetByCopy(-3, -3).Contains(where))
            return kCancelDevice;
    } else if (fProgressRect.InsetByCopy(-4, -7).Contains(where) && fState != kStopped)
        return kProgress;
    if (fViewRect.Contains(where)) {
        float w = fViewRect.Width() / 3;
        int index = (int)((where.x - fViewRect.left) / w);
        return index <= 0 ? kViewList : (index == 1 ? kViewGrouped : kViewGrid);
    }
    return kNone;
}

float ToolbarView::VolumeFromPoint(BPoint where) const
{
    float v = (where.x - fVolumeRect.left) / fVolumeRect.Width();
    return std::min(1.0f, std::max(0.0f, v));
}

void ToolbarView::MouseDown(BPoint where)
{
    MakeFocus(false);
    fPressed = HitTest(where);
    if (fPressed == kNone)
        return;
    fTracking = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
    if (fPressed == kVolume) {
        fVolume = VolumeFromPoint(where);
        BMessage message(kMsgVolumeChanged);
        message.AddFloat("volume", fVolume);
        Window()->PostMessage(&message);
    } else if (fPressed == kProgress && fDuration > 0) {
        float fraction = std::min(1.0f, std::max(0.0f, (where.x - fProgressRect.left - 5) / (fProgressRect.Width() - 10)));
        fPosition = (int64_t)(fraction * fDuration);
    }
    Invalidate();
}

void ToolbarView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
    if (!fTracking)
        return;
    if (fPressed == kVolume) {
        fVolume = VolumeFromPoint(where);
        BMessage message(kMsgVolumeChanged);
        message.AddFloat("volume", fVolume);
        Window()->PostMessage(&message);
        Invalidate(fVolumeRect.InsetByCopy(-20, -8));
    } else if (fPressed == kProgress && fDuration > 0) {
        float fraction = std::min(1.0f, std::max(0.0f, (where.x - fProgressRect.left - 5) / (fProgressRect.Width() - 10)));
        fPosition = (int64_t)(fraction * fDuration);
        Invalidate(fLcdRect);
    }
}

void ToolbarView::MouseUp(BPoint where)
{
    if (!fTracking)
        return;
    fTracking = false;
    Hot released = HitTest(where);
    switch (fPressed) {
        case kPrev:
            if (released == kPrev)
                Window()->PostMessage(kMsgPrevious);
            break;
        case kPlay:
            if (released == kPlay)
                Window()->PostMessage(kMsgPlayPause);
            break;
        case kNext:
            if (released == kNext)
                Window()->PostMessage(kMsgNext);
            break;
        case kProgress: {
            BMessage message(kMsgSeek);
            message.AddInt64("position", fPosition);
            Window()->PostMessage(&message);
            break;
        }
        case kCancelDevice:
            if (released == kCancelDevice)
                Window()->PostMessage(kMsgMDCancel);
            break;
        case kViewList:
        case kViewGrouped:
        case kViewGrid:
            if (released == fPressed) {
                BMessage message(kMsgViewMode);
                message.AddInt32("mode", fPressed == kViewList ? 0 : (fPressed == kViewGrouped ? 1 : 2));
                Window()->PostMessage(&message);
            }
            break;
        default:
            break;
    }
    fPressed = kNone;
    Invalidate();
}

} // namespace amp
