// Top toolbar: transport buttons, volume slider, LCD-style display, view switcher and search.
#pragma once
#include "core/Model.h"
#include "player/Messages.h"
#include <String.h>
#include <TextControl.h>
#include <View.h>

namespace amp {

class ToolbarView : public BView {
public:
    // What the display shows while a MiniDisc is written, in place of the playing song.
    struct DeviceStatus {
        bool active = false;
        BString headline, detail;
        float fraction = -1;          // overall progress; below zero the bar animates
        BString leftLabel, rightLabel; // either side of the bar
        bool cancellable = false;
        bool done = false;            // finished: a check mark instead of the cancel button
        bool failed = false;
    };

    ToolbarView();
    ~ToolbarView() override;
    void SetDeviceStatus(const DeviceStatus& status);
    const DeviceStatus& GetDeviceStatus() const { return fDevice; }

    void SetPlayerState(PlayerState state);
    void SetTrackInfo(const BString& title, const BString& artist, const BString& album, bool isMA);
    // Artwork of the playing track (key plus the request used to fetch it when missing).
    void SetArtwork(const ArtRequest& request);
    // "Lossless", "320 kbps" or empty.
    void SetQuality(const BString& quality);
    void ArtworkChanged() { Invalidate(fLcdRect); }
    void SetProgress(int64_t positionMs, int64_t durationMs);
    void SetVolume(float volume);
    void SetViewMode(int mode);
    void SetLoadingText(const BString& text);
    BTextControl* SearchField() { return fSearch; }

    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void FrameResized(float width, float height) override;
    void MessageReceived(BMessage* message) override;
    void DetachedFromWindow() override;

private:
    enum Hot { kNone, kPrev, kPlay, kNext, kVolume, kProgress, kViewList, kViewGrouped, kViewGrid, kCancelDevice };
    void DrawDeviceStatus(BRect lcd);
    BRect CancelRect() const;
    void Layout();
    void DrawRoundButton(BRect rect, Hot which);
    void DrawLcd();
    void DrawProgress(BRect lcd);
    void DrawVolume();
    void DrawViewButtons();
    Hot HitTest(BPoint where) const;
    float VolumeFromPoint(BPoint where) const;

    BTextControl* fSearch;
    PlayerState fState = kStopped;
    BString fTitle, fArtist, fAlbum, fLoading, fQuality;
    ArtRequest fArt;
    BRect fArtRect;
    bool fIsMA = false;
    int64_t fPosition = 0, fDuration = 0;
    float fVolume = 0.8f;
    int fViewMode = 0;
    Hot fPressed = kNone;
    bool fTracking = false;
    BRect fPrevRect, fPlayRect, fNextRect, fVolumeRect, fLcdRect, fProgressRect, fViewRect, fSearchRect;
    DeviceStatus fDevice;
    float fStripePhase = 0;
    class BMessageRunner* fPulse = nullptr;
};

} // namespace amp
