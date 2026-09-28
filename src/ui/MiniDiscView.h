// The MiniDisc source: what is on the disc, how full it is, and the songs a write is adding.
#pragma once
#include "player/MiniDisc.h"
#include <String.h>
#include <View.h>
#include <vector>

class BButton;
class BScrollView;

namespace amp {

class MiniDiscSummaryView;
class MiniDiscListView;

class MiniDiscView : public BView {
public:
    MiniDiscView();

    void SetState(const MiniDiscState& state);
    // A write started: `titles` and `durations` are the songs it adds; `erase` hides the disc's
    // current tracks, which are about to go.
    void BeginWrite(const std::vector<BString>& titles, const std::vector<int64_t>& durationsMs, bool erase);
    // Progress of the running write: song `track` (1-based) of the write is in `phase`.
    void SetWriteProgress(int track, int phase, float trackFraction);
    void EndWrite();

private:
    MiniDiscSummaryView* fSummary;
    MiniDiscListView* fList;
    BScrollView* fScroll;
    bool fErasing = false;
};

} // namespace amp
