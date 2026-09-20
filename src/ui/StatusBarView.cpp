#include "StatusBarView.h"
#include "Icons.h"
#include "Theme.h"
#include "player/Messages.h"
#include <Font.h>
#include <Window.h>

namespace amp {

namespace {
const uint32 kMsgExpire = 'stex';

// The Settings button is the right-most element of the bar; the MA indicator sits to its left.
const float kSettingsButtonWidth = 78.0f;
const float kStatusBarInset = 8.0f;
const float kIndicatorWidth = 200.0f;
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

BRect StatusBarView::SettingsRect() const
{
    BRect bounds = Bounds();
    float right = bounds.right - kStatusBarInset;
    return BRect(right - kSettingsButtonWidth, 3, right, theme::kStatusBarHeight - 4);
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
        icons::Icon glyph = i == 0 ? icons::kPlus : (i == 1 ? icons::kShuffle : icons::kRepeat);
        icons::Draw(this, glyph, r, 11, active ? Rgb(255, 255, 255) : Rgb(60, 60, 60));
        if (i == 2 && fRepeat == 2) {
            // repeat one: a small marker on the repeat glyph
            BFont small(be_bold_font);
            small.SetSize(8);
            SetFont(&small);
            SetHighColor(active ? Rgb(255, 255, 255) : Rgb(60, 60, 60));
            SetDrawingMode(B_OP_OVER);
            float cx = (r.left + r.right) / 2;
            float cy = (r.top + r.bottom) / 2;
            DrawString("1", BPoint(cx + 1, cy + 6));
        }
    }
    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(theme::kToolbarText);
    BString center = fTransient.IsEmpty() ? fSummary : fTransient;
    // the indicator and the Settings button own the right-hand end: keep the summary clear of them
    BRect settings = SettingsRect();
    float indicatorRight = settings.left - 12;
    float indicatorLeft = indicatorRight - kIndicatorWidth;
    float centerRight = indicatorLeft - 12;
    if (centerRight < 120)
        centerRight = 120;
    DrawTruncated(this, center.String(), BRect(120, 0, centerRight, bounds.bottom), B_ALIGN_CENTER, 0);
    // Music Assistant indicator left of the Settings button
    BRect indicator(indicatorLeft, 0, indicatorRight, bounds.bottom);
    SetHighColor(fMAConnected ? Rgb(60, 170, 80) : Rgb(150, 150, 150));
    FillEllipse(BRect(indicator.left, 8, indicator.left + 8, 16));
    SetHighColor(theme::kToolbarText);
    BString text = fMAText.IsEmpty() ? BString(fMAConnected ? "Music Assistant connected" : "Music Assistant off") : fMAText;
    DrawTruncated(this, text.String(), BRect(indicator.left + 12, 0, indicator.right, bounds.bottom), B_ALIGN_LEFT, 0);
    DrawSettingsButton();
}

void StatusBarView::DrawSettingsButton()
{
    BRect r = SettingsRect();
    bool pressed = fPressed == kSettings;
    if (pressed)
        FillRoundGradient(this, r, 4, Rgb(140, 140, 140), Rgb(170, 170, 170));
    else
        FillRoundGradient(this, r, 4, Rgb(250, 250, 250), Rgb(205, 205, 205));
    SetHighColor(120, 120, 120);
    StrokeRoundRect(r, 4, 4);

    // the Font Awesome gear, centred on its ink
    float cy = (r.top + r.bottom) / 2;
    icons::Draw(this, icons::kGear, BRect(r.left + 5, cy - 7, r.left + 19, cy + 7), 13,
        pressed ? Rgb(255, 255, 255) : Rgb(70, 70, 70));

    BFont font(be_plain_font);
    font.SetSize(11);
    SetFont(&font);
    SetHighColor(pressed ? Rgb(255, 255, 255) : theme::kToolbarText);
    SetDrawingMode(B_OP_OVER);
    DrawTruncated(this, "Settings", BRect(r.left + 22, r.top, r.right - 6, r.bottom), B_ALIGN_LEFT, 0);
    SetDrawingMode(B_OP_COPY);
}

StatusBarView::Hot StatusBarView::HitTest(BPoint where) const
{
    for (int i = 0; i < 3; i++)
        if (ButtonRect(i).Contains(where))
            return (Hot)(i + 1);
    if (SettingsRect().Contains(where))
        return kSettings;
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
        else if (fPressed == kRepeat)
            Window()->PostMessage(kMsgToggleRepeat);
        else
            Window()->PostMessage(kMsgShowSettings);
    }
    fPressed = kNone;
    Invalidate();
}

} // namespace amp
