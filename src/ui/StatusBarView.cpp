#include "StatusBarView.h"
#include "Theme.h"
#include "player/Messages.h"
#include <Font.h>
#include <Window.h>

namespace tasamp {

namespace {
const uint32 kMsgExpire = 'stex';
}

StatusBarView::StatusBarView()
    : BView("statusbar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetExplicitMinSize(BSize(300, theme::kStatusBarHeight));
    SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, theme::kStatusBarHeight));
}

void StatusBarView::SetSummary(const BString& text)
{
    fSummary = text;
    Invalidate();
}

void StatusBarView::SetTransient(const BString& text, int seconds)
{
    fTransient = text;
    delete fExpiry;
    fExpiry = nullptr;
    if (seconds > 0 && !text.IsEmpty() && Window()) {
        BMessage expire(kMsgExpire);
        fExpiry = new BMessageRunner(BMessenger(this), &expire, (bigtime_t)seconds * 1000000, 1);
    }
    Invalidate();
}

void StatusBarView::MessageReceived(BMessage* message)
{
    if (message->what == kMsgExpire) {
        fTransient = "";
        Invalidate();
        return;
    }
    BView::MessageReceived(message);
}

void StatusBarView::DetachedFromWindow()
{
    delete fExpiry;
    fExpiry = nullptr;
    BView::DetachedFromWindow();
}

void StatusBarView::SetMAStatus(bool connected, const BString& text)
{
    fMAConnected = connected;
    fMAText = text;
    Invalidate();
}

void StatusBarView::SetShuffle(bool shuffle)
{
    fShuffle = shuffle;
    Invalidate();
}

void StatusBarView::SetRepeat(int repeat)
{
    fRepeat = repeat;
    Invalidate();
}

BRect StatusBarView::ButtonRect(int index) const
{
    float left = 6 + index * 34;
    return BRect(left, 3, left + 30, theme::kStatusBarHeight - 4);
}

void StatusBarView::Draw(BRect updateRect)
{
    BRect bounds = Bounds();
    FillVerticalGradient(this, bounds, Rgb(226, 226, 226), Rgb(190, 190, 190));
    SetHighColor(theme::kToolbarBorder);
    StrokeLine(bounds.LeftTop(), bounds.RightTop());
    for (int i = 0; i < 3; i++) {
        BRect r = ButtonRect(i);
        bool active = (i == 1 && fShuffle) || (i == 2 && fRepeat != 0);
        bool pressed = fPressed == (Hot)(i + 1);
        if (active || pressed)
            FillRoundGradient(this, r, 4, Rgb(140, 140, 140), Rgb(170, 170, 170));
        else
            FillRoundGradient(this, r, 4, Rgb(250, 250, 250), Rgb(205, 205, 205));
        SetHighColor(120, 120, 120);
        StrokeRoundRect(r, 4, 4);
        SetHighColor(active ? Rgb(255, 255, 255) : Rgb(60, 60, 60));
        float cx = (r.left + r.right) / 2;
        float cy = (r.top + r.bottom) / 2;
        if (i == 0) {
            FillRect(BRect(cx - 5, cy - 1, cx + 5, cy));
            FillRect(BRect(cx - 0.5f, cy - 5, cx + 0.5f, cy + 5));
        } else if (i == 1) {
            SetPenSize(1.5f);
            StrokeLine(BPoint(cx - 7, cy - 4), BPoint(cx - 3, cy - 4));
            StrokeLine(BPoint(cx - 3, cy - 4), BPoint(cx + 3, cy + 4));
            StrokeLine(BPoint(cx + 3, cy + 4), BPoint(cx + 7, cy + 4));
            StrokeLine(BPoint(cx - 7, cy + 4), BPoint(cx - 3, cy + 4));
            StrokeLine(BPoint(cx - 3, cy + 4), BPoint(cx + 3, cy - 4));
            StrokeLine(BPoint(cx + 3, cy - 4), BPoint(cx + 7, cy - 4));
            SetPenSize(1);
        } else {
            SetPenSize(1.5f);
            StrokeArc(BPoint(cx, cy), 6, 4.5f, 30, 300);
            BPoint tri[3] = {BPoint(cx + 3, cy - 7), BPoint(cx + 7, cy - 4), BPoint(cx + 3, cy - 1)};
            FillPolygon(tri, 3);
            SetPenSize(1);
            if (fRepeat == 2) {
                BFont small(be_bold_font);
                small.SetSize(8);
                SetFont(&small);
                DrawString("1", BPoint(cx - 2.5f, cy + 3));
            }
        }
    }
    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(theme::kToolbarText);
    BString center = fTransient.IsEmpty() ? fSummary : fTransient;
    DrawTruncated(this, center.String(), BRect(120, 0, bounds.right - 220, bounds.bottom), B_ALIGN_CENTER, 0);
    // Music Assistant indicator on the right
    BRect indicator(bounds.right - 210, 0, bounds.right - 8, bounds.bottom);
    SetHighColor(fMAConnected ? Rgb(60, 170, 80) : Rgb(150, 150, 150));
    FillEllipse(BRect(indicator.left, 8, indicator.left + 8, 16));
    SetHighColor(theme::kToolbarText);
    BString text = fMAText.IsEmpty() ? BString(fMAConnected ? "Music Assistant connected" : "Music Assistant off") : fMAText;
    DrawTruncated(this, text.String(), BRect(indicator.left + 12, 0, indicator.right, bounds.bottom), B_ALIGN_LEFT, 0);
}

StatusBarView::Hot StatusBarView::HitTest(BPoint where) const
{
    for (int i = 0; i < 3; i++)
        if (ButtonRect(i).Contains(where))
            return (Hot)(i + 1);
    return kNone;
}

void StatusBarView::MouseDown(BPoint where)
{
    fPressed = HitTest(where);
    if (fPressed != kNone) {
        SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
        Invalidate();
    }
}

void StatusBarView::MouseUp(BPoint where)
{
    Hot released = HitTest(where);
    if (released == fPressed && fPressed != kNone) {
        if (fPressed == kAdd)
            Window()->PostMessage(kMsgNewPlaylist);
        else if (fPressed == kShuffle)
            Window()->PostMessage(kMsgToggleShuffle);
        else
            Window()->PostMessage(kMsgToggleRepeat);
    }
    fPressed = kNone;
    Invalidate();
}

} // namespace tasamp
