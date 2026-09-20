// Artists browser: artist list with images on the left, the artist's albums and songs on the right.
#pragma once
#include "TrackListView.h"
#include <String.h>
#include <View.h>
#include <vector>

namespace amp {

const uint32 kMsgArtistSelected = 'arsl';

class ArtistListView : public BView {
public:
    ArtistListView();
    void SetOwner(BHandler* owner) { fOwner = owner; }
    void SetArtists(const std::vector<int64_t>& ids);
    void Select(int64_t artistId, bool notify);
    int64_t Selected() const { return fSelected; }

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void KeyDown(const char* bytes, int32 numBytes) override;
    void FrameResized(float width, float height) override;
    void WindowActivated(bool active) override { Invalidate(); }

private:
    void UpdateScrollBar();
    int IndexAt(BPoint where) const;
    void ScrollToIndex(int index);

    std::vector<int64_t> fArtists;
    int64_t fSelected = 0;
    BHandler* fOwner = nullptr;
};

class ArtistHeaderView : public BView {
public:
    ArtistHeaderView();
    void SetArtist(int64_t artistId);
    void Draw(BRect updateRect) override;

private:
    int64_t fArtist = 0;
};

class ArtistsView : public BView {
public:
    ArtistsView();
    void SetArtists(const std::vector<int64_t>& ids);
    void SelectArtist(int64_t artistId);
    int64_t SelectedArtist() const { return fList->Selected(); }
    TrackListView* TrackList() { return fTracks; }
    void Refresh();
    void ReloadTracks();
    void MessageReceived(BMessage* message) override;

private:
    ArtistListView* fList;
    ArtistHeaderView* fHeader;
    TrackListView* fTracks;
};

} // namespace amp
