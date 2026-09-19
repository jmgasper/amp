#include "ToolbarView.h"
#include "App.h"
#include "Theme.h"
#include "core/Model.h"
#include <Bitmap.h>
#include <Font.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Message.h>
#include <Window.h>
#include <cstdio>

namespace tasamp {

ToolbarView::ToolbarView()
    : BView("toolbar", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(640, theme::kToolbarHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, theme::kToolbarHeight));
    SetExplicitPreferredSize(BSize(1000, theme::kToolbarHeight));
    fSearch = new BTextControl("search", nullptr, "", new BMessage(kMsgSearch));
    fSearch->SetModificationMessage(new BMessage(kMsgSearch));
    AddChild(fSearch);
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
    fVolumeRect = BRect(150, cy - 8, 270, cy + 8);
    float viewWidth = 96;
    float searchWidth = std::min(180.0f, std::max(120.0f, bounds.Width() * 0.15f));
    fSearchRect = BRect(bounds.right - searchWidth - 14, 14, bounds.right - 14, 38);
    fViewRect = BRect(fSearchRect.left - viewWidth - 24, 16, fSearchRect.left - 24, 36);
    float lcdLeft = 300;
    float lcdRight = fViewRect.left - 30;
    float lcdWidth = std::max(260.0f, std::min(560.0f, lcdRight - lcdLeft));
    float lcdCenter = floorf((lcdLeft + lcdRight) / 2);
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
    // glyph
    SetHighColor(60, 60, 60);
    float cx = (rect.left + rect.right) / 2;
    float cy = (rect.top + rect.bottom) / 2;
    float s = rect.Width() * 0.17f;
    if (which == kPlay) {
        if (fState == kPlaying) {
            FillRect(BRect(cx - s - 1, cy - s - 1, cx - 1.5f, cy + s));
            FillRect(BRect(cx + 1.5f, cy - s - 1, cx + s + 1, cy + s));
        } else {
            BPoint tri[3] = {BPoint(cx - s + 1, cy - s - 1), BPoint(cx + s + 2, cy), BPoint(cx - s + 1, cy + s + 1)};
            FillPolygon(tri, 3);
        }
    } else if (which == kPrev) {
        FillRect(BRect(cx - s - 1, cy - s, cx - s, cy + s));
        BPoint tri[3] = {BPoint(cx + 0.5f, cy - s), BPoint(cx - s, cy), BPoint(cx + 0.5f, cy + s)};
        FillPolygon(tri, 3);
        BPoint tri2[3] = {BPoint(cx + s + 1.5f, cy - s), BPoint(cx + 1, cy), BPoint(cx + s + 1.5f, cy + s)};
        FillPolygon(tri2, 3);
    } else if (which == kNext) {
        FillRect(BRect(cx + s, cy - s, cx + s + 1, cy + s));
        BPoint tri[3] = {BPoint(cx - 0.5f, cy - s), BPoint(cx + s, cy), BPoint(cx - 0.5f, cy + s)};
        FillPolygon(tri, 3);
        BPoint tri2[3] = {BPoint(cx - s - 1.5f, cy - s), BPoint(cx - 1, cy), BPoint(cx - s - 1.5f, cy + s)};
        FillPolygon(tri2, 3);
    }
}

void ToolbarView::DrawVolume()
{
    BRect r = fVolumeRect;
    float cy = (r.top + r.bottom) / 2;
    // small and large speaker glyphs
    auto speaker = [&](BPoint origin, float scale) {
        SetHighColor(90, 90, 90);
        FillRect(BRect(origin.x, cy - 2 * scale, origin.x + 2 * scale, cy + 2 * scale));
        BPoint cone[4] = {BPoint(origin.x + 2 * scale, cy - 2 * scale), BPoint(origin.x + 5 * scale, cy - 5 * scale),
            BPoint(origin.x + 5 * scale, cy + 5 * scale), BPoint(origin.x + 2 * scale, cy + 2 * scale)};
        FillPolygon(cone, 4);
    };
    speaker(BPoint(r.left - 12, 0), 0.8f);
    speaker(BPoint(r.right + 6, 0), 1.2f);
    SetHighColor(120, 120, 120);
    StrokeArc(BPoint(r.right + 13, cy), 4, 4, -50, 100);
    StrokeArc(BPoint(r.right + 13, cy), 7, 7, -50, 100);
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

    BFont bold(be_bold_font);
    bold.SetSize(12);
    BFont plain(be_plain_font);
    plain.SetSize(11);
    if (fState == kStopped || fTitle.IsEmpty()) {
        // idle: the TasAmp mark
        BRect note(r.left + r.Width() / 2 - 30, r.top + 13, r.left + r.Width() / 2 - 10, r.top + 39);
        DrawNoteIcon(this, note, theme::kLcdSecondary);
        bold.SetSize(17);
        SetFont(&bold);
        SetHighColor(theme::kLcdSecondary);
        DrawString("TasAmp", BPoint(r.left + r.Width() / 2 - 6, r.top + 33));
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
        SetHighColor(active ? Rgb(255, 255, 255) : Rgb(70, 70, 70));
        float cx = (cell.left + cell.right) / 2;
        float cy = (cell.top + cell.bottom) / 2;
        if (i == 0) {
            for (int line = -1; line <= 1; line++)
                FillRect(BRect(cx - 7, cy + line * 4 - 1, cx + 7, cy + line * 4));
        } else if (i == 1) {
            FillRect(BRect(cx - 8, cy - 5, cx - 2, cy + 5));
            for (int line = -1; line <= 1; line++)
                FillRect(BRect(cx, cy + line * 4 - 1, cx + 8, cy + line * 4));
        } else {
            for (int gx = -1; gx <= 0; gx++)
                for (int gy = -1; gy <= 0; gy++)
                    FillRect(BRect(cx + gx * 8 + 1, cy + gy * 8 + 1, cx + gx * 8 + 6, cy + gy * 8 + 6));
        }
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
    if (fProgressRect.InsetByCopy(-4, -7).Contains(where) && fState != kStopped)
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

} // namespace tasamp
