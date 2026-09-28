// Amp's MiniDisc support: watches for NetMD recorders, keeps the inserted disc's state current
// and writes songs to it. All device access happens on one worker thread; the window learns
// about changes through messages and reads the state snapshot.
#pragma once
#include "core/NetMD.h"
#include <Messenger.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace amp {

class NetMDRoster;

struct MiniDiscState {
    bool connected = false;        // a recorder is on the bus
    std::string deviceName;
    bool known = false;            // `disc` has been read since the recorder appeared
    bool contentsKnown = false;    // `disc.tracks` lists the tracks
    netmd::DiscInfo disc;
    bool busy = false;             // a write or erase is running
    std::string error;             // why the recorder cannot be used, if it cannot
};

struct MiniDiscWriteItem {
    int64_t trackId = 0;
    std::string path;              // local audio file
    std::string title;             // title for the disc, already sanitised
    int64_t durationMs = 0;
};

struct MiniDiscJob {
    std::string name;              // playlist or album name, for the progress display
    std::vector<MiniDiscWriteItem> items;
    bool erase = false;            // erase the disc first
    std::string discTitle;         // empty: leave the disc title alone
};

// Progress phases (the "phase" field of kMsgMDProgress).
enum MiniDiscPhase { kMDPreparing = 0, kMDErasing, kMDConverting, kMDWriting, kMDFinishing };

class MiniDiscManager {
public:
    MiniDiscManager();
    ~MiniDiscManager();

    void Start(const BMessenger& target);
    // Stops watching. A worker stuck in a USB transfer is left behind rather than waited for.
    void Stop();

    MiniDiscState State();
    bool Busy() const;
    // Re-reads the disc; `contents` also lists its tracks.
    void Refresh(bool contents);
    // Queues a write; false when the recorder is busy or gone.
    bool Write(const MiniDiscJob& job);
    void Erase();
    // Stops a write after the song being transferred.
    void Cancel();
    bool Cancelling() const;

    struct Shared;

private:
    std::shared_ptr<Shared> fShared;
    std::unique_ptr<NetMDRoster> fRoster;
};

} // namespace amp
