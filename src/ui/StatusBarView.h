// Bottom status bar: playlist/shuffle/repeat buttons, library summary, the MA indicator
// and the Settings button at the right-hand end.
#pragma once
#include <MessageRunner.h>
#include <String.h>
#include <View.h>

namespace amp {

class StatusBarView : public BView {
public:
    StatusBarView();
    void SetSummary(const BString& text);
    // Shows a message in place of the summary; it clears itself after `seconds` (0 = stays).
    void SetTransient(const BString& text, int seconds = 6);
    void MessageReceived(BMessage* message) override;
    void DetachedFromWindow() override;
    void SetMAStatus(bool connected, const BString& text);
    // The indicator is shown while Music Assistant is switched on in the settings.
    void SetMAVisible(bool visible);
    void SetShuffle(bool shuffle);
    void SetRepeat(int repeat);
    // The "Write to MiniDisc" button (iTunes' "Burn Disc"): shown while a recorder is connected
    // and the view holds something to write.
    void SetMiniDiscButton(bool visible, bool enabled);

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;

private:
    enum Hot { kNone, kAdd, kShuffle, kRepeat, kSettings, kMiniDisc };
    Hot HitTest(BPoint where) const;
    BRect ButtonRect(int index) const;
    BRect SettingsRect() const;
    BRect MiniDiscRect() const;
    void DrawSettingsButton();
    void DrawMiniDiscButton();

    BString fSummary, fTransient, fMAText;
    bool fMAConnected = false;
    bool fMAVisible = true;
    bool fShuffle = false;
    int fRepeat = 0;
    bool fMiniDiscVisible = false;
    bool fMiniDiscEnabled = false;
    Hot fPressed = kNone;
    BMessageRunner* fExpiry = nullptr;
};

} // namespace amp
