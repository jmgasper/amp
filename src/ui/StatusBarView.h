// Bottom status bar: playlist/shuffle/repeat buttons, library summary and the MA indicator.
#pragma once
#include <MessageRunner.h>
#include <String.h>
#include <View.h>

namespace tasamp {

class StatusBarView : public BView {
public:
    StatusBarView();
    void SetSummary(const BString& text);
    // Shows a message in place of the summary; it clears itself after `seconds` (0 = stays).
    void SetTransient(const BString& text, int seconds = 6);
    void MessageReceived(BMessage* message) override;
    void DetachedFromWindow() override;
    void SetMAStatus(bool connected, const BString& text);
    void SetShuffle(bool shuffle);
    void SetRepeat(int repeat);

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;

private:
    enum Hot { kNone, kAdd, kShuffle, kRepeat };
    Hot HitTest(BPoint where) const;
    BRect ButtonRect(int index) const;

    BString fSummary, fTransient, fMAText;
    bool fMAConnected = false;
    bool fShuffle = false;
    int fRepeat = 0;
    Hot fPressed = kNone;
    BMessageRunner* fExpiry = nullptr;
};

} // namespace tasamp
