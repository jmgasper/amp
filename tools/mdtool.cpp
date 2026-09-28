// mdtool: exercises Amp's NetMD code from the command line.
//   mdtool info | list | erase | title <text> | write <file> [title] | pcm <file> <out.raw>
#include "core/NetMD.h"
#include "player/MiniDiscPcm.h"
#include "player/NetMDUsb.h"
#include <OS.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

using namespace amp;

static bool FindRecorder(NetMDDeviceId& found)
{
    std::mutex lock;
    bool have = false;
    NetMDRoster roster;
    roster.onAdded = [&](const NetMDDeviceId& id) {
        std::lock_guard<std::mutex> guard(lock);
        if (!have) {
            found = id;
            have = true;
        }
    };
    roster.Start();
    for (int i = 0; i < 20 && !have; i++)
        snooze(50000);
    roster.Stop();
    return have;
}

static void PrintInfo(const netmd::DiscInfo& info)
{
    if (!info.present) {
        printf("no disc\n");
        return;
    }
    printf("title: \"%s\" (raw \"%s\")\n", info.title.c_str(), info.rawTitle.c_str());
    printf("tracks: %d  used %s  total %s  left %s  (frames %lld/%lld/%lld)\n", info.trackCount,
        netmd::FormatFrames(info.usedFrames).c_str(), netmd::FormatFrames(info.totalFrames).c_str(),
        netmd::FormatFrames(info.leftFrames).c_str(), (long long)info.usedFrames, (long long)info.totalFrames,
        (long long)info.leftFrames);
    printf("writable %d, write-protected %d\n", info.writable, info.writeProtected);
    for (const netmd::TrackInfo& track : info.tracks)
        printf("  %2d. %-40s %s (%lld frames) enc %02x%s\n", track.index + 1, track.title.c_str(),
            netmd::FormatFrames(track.frames).c_str(), (long long)track.frames, track.encoding, track.mono ? " mono" : "");
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mdtool info|list|erase|title <text>|write <file> [title]|pcm <file> <out>\n");
        return 2;
    }
    const char* command = argv[1];
    if (!strcmp(command, "pcm") && argc >= 4) {
        std::string error;
        auto start = std::chrono::steady_clock::now();
        int64_t bytes = ConvertToMiniDiscPcm(argv[2], argv[3], [](float) { return true; }, error);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (bytes < 0) {
            fprintf(stderr, "conversion failed: %s\n", error.c_str());
            return 1;
        }
        printf("%lld bytes (%.2f s of audio) in %.2f s\n", (long long)bytes, bytes / 176400.0, seconds);
        return 0;
    }

    NetMDDeviceId id;
    if (!FindRecorder(id)) {
        fprintf(stderr, "no NetMD recorder found\n");
        return 1;
    }
    printf("recorder: %s (%04x:%04x) at %s\n", id.name.c_str(), id.vendor, id.product, id.path.c_str());
    NetMDUsbTransport transport;
    std::string error;
    if (!transport.Open(id.path.c_str(), error)) {
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    netmd::Device device(transport);
    auto started = std::chrono::steady_clock::now();
    if (getenv("MDTOOL_LOG"))
        device.log = [&](const std::string& line) {
            fprintf(stderr, "%8.3f %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), line.c_str());
        };
    try {
        device.Flush();
        if (!strcmp(command, "info") || !strcmp(command, "list")) {
            auto start = std::chrono::steady_clock::now();
            netmd::DiscInfo info = device.ReadDiscInfo();
            if (!strcmp(command, "list"))
                device.ReadContents(info);
            printf("state %04x\n", device.OperatingState());
            PrintInfo(info);
            printf("(read in %.2f s)\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        } else if (!strcmp(command, "erase")) {
            device.Acquire();
            device.EraseDisc();
            device.Release();
            printf("erased\n");
        } else if (!strcmp(command, "title") && argc >= 3) {
            device.Acquire();
            device.SetDiscTitle(netmd::SanitizeTitle(argv[2]));
            device.Release();
            printf("title set\n");
        } else if (!strcmp(command, "write") && argc >= 3) {
            std::string pcm = "/tmp/mdtool.pcm";
            auto start = std::chrono::steady_clock::now();
            int64_t bytes = ConvertToMiniDiscPcm(argv[2], pcm, [](float) { return true; }, error);
            if (bytes < 0) {
                fprintf(stderr, "conversion failed: %s\n", error.c_str());
                return 1;
            }
            printf("converted: %lld bytes (%.2f s) in %.2f s\n", (long long)bytes, bytes / 176400.0,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
            PcmFileSource source(pcm);
            std::string title = netmd::SanitizeTitle(argc >= 4 ? argv[3] : argv[2]);
            start = std::chrono::steady_clock::now();
            int lastPercent = -1;
            int track = netmd::DownloadTrack(device, source, (uint64_t)bytes, title, [&](const netmd::DownloadProgress& p) {
                int percent = (int)(p.sent * 100 / std::max<uint64_t>(p.total, 1));
                if (percent / 10 != lastPercent / 10) {
                    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    printf("  %3d%%  %.1f s  %.0f KB/s\n", percent, elapsed, p.sent / 1024.0 / std::max(elapsed, 0.001));
                    fflush(stdout);
                    lastPercent = percent;
                }
            });
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            printf("wrote track %d \"%s\" in %.1f s (%.2fx real time)\n", track + 1, title.c_str(), elapsed,
                (bytes / 176400.0) / elapsed);
            remove(pcm.c_str());
        } else {
            fprintf(stderr, "unknown command\n");
            return 2;
        }
    } catch (const netmd::Error& e) {
        fprintf(stderr, "NetMD error (%d): %s\n", (int)e.kind(), e.what());
        netmd::Recover(device);
        return 1;
    }
    return 0;
}
