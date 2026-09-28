// NetMD: Sony's USB protocol for MiniDisc recorders. This is the portable part: command
// encoding, reply parsing, the secure download session and the helpers that plan a write
// (title sanitising, the title-cell budget, disc capacity). The USB transfers themselves come
// from a Transport, which the Haiku side implements with the USB Kit.
//
// Written from the protocol as documented by the linux-minidisc and netmd-js projects.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <vector>

namespace amp {
namespace netmd {

using Bytes = std::vector<uint8_t>;

// Raw access to a NetMD device: vendor control transfers to interface 0 and the bulk OUT pipe.
class Transport {
public:
    virtual ~Transport() {}
    // bmRequestType 0xc1 (vendor, interface, device to host). Returns the byte count or < 0.
    virtual ssize_t VendorIn(uint8_t request, void* data, size_t length) = 0;
    // bmRequestType 0x41 (vendor, interface, host to device).
    virtual ssize_t VendorOut(uint8_t request, const void* data, size_t length) = 0;
    virtual ssize_t BulkOut(const void* data, size_t length) = 0;
    virtual void Sleep(int milliseconds);
};

class Error : public std::runtime_error {
public:
    enum Kind { kIo, kRejected, kNotImplemented, kProtocol, kTimeout, kCancelled };
    Error(Kind kind, const std::string& what) : std::runtime_error(what), fKind(kind) {}
    Kind kind() const { return fKind; }

private:
    Kind fKind;
};

// Builds a command: hex text ("1806 0210 ff00", spaces ignored) and big-endian values.
class Query {
public:
    Query() {}
    explicit Query(const char* hex) { Hex(hex); }
    Query& Hex(const char* hex);
    Query& U8(unsigned value);
    Query& U16(unsigned value);
    Query& U32(uint32_t value);
    Query& Append(const Bytes& bytes);
    const Bytes& Data() const { return fData; }

private:
    Bytes fData;
};

// Walks a reply. Expect() checks fixed bytes ("??" matches any byte) and throws kProtocol on
// a mismatch.
class Reply {
public:
    explicit Reply(Bytes data) : fData(std::move(data)) {}
    Reply& Expect(const char* pattern);
    uint8_t U8();
    uint16_t U16();
    uint32_t U32();
    int Bcd8();
    int Bcd16();
    Bytes Take(size_t count);
    Bytes Prefixed16(); // two length bytes, then that many bytes
    Bytes Rest();
    size_t Remaining() const { return fData.size() - fPosition; }

private:
    void Need(size_t count) const;
    Bytes fData;
    size_t fPosition = 0;
};

// Time on a disc as the device reports it: frames of 1/512 second.
const int kFramesPerSecond = 512;

enum DiscFormat { kDiscLP4 = 0, kDiscLP2 = 2, kDiscSPMono = 4, kDiscSPStereo = 6 };
enum Encoding { kEncodingSP = 0x90, kEncodingLP2 = 0x92, kEncodingLP4 = 0x93 };
enum WireFormat { kWirePCM = 0x00 };
const size_t kPcmFrameSize = 2048;          // one "frame" of the PCM wire format
const double kSlowLatencyMs = 2.5;          // above this, SP uploads cannot keep up
const int kPcmBytesPerSecond = 44100 * 4;   // 16-bit big-endian stereo at 44.1 kHz

struct TrackInfo {
    int index = 0;             // 0-based position on the disc
    std::string title;
    int64_t frames = 0;        // length in 1/512 s
    int encoding = kEncodingSP;
    bool mono = false;
    bool isProtected = false;
};

struct DiscInfo {
    bool present = false;
    bool writable = false;
    bool writeProtected = false;
    std::string title;         // the disc title without group information
    std::string rawTitle;      // as stored, "0;Title//1-3;Group//" when the disc has groups
    int trackCount = 0;
    int64_t usedFrames = 0, totalFrames = 0, leftFrames = 0;
    std::vector<TrackInfo> tracks; // filled by ReadContents
};

// Commands of a NetMD device. Every call is synchronous and throws Error on failure.
class Device {
public:
    explicit Device(Transport& transport) : fTransport(transport) {}

    // Reads and drops a reply a previous session may have left behind.
    void Flush();
    // Median time of a minimal control transfer, in milliseconds. Through a controller that
    // waits for a 1 ms frame per USB transaction it takes three or more, and such a port cannot
    // feed an SP recording in real time; elsewhere it takes about one or less.
    double MeasureLatency(int rounds = 20);
    // Sends one command (the status byte is added here) and returns the reply without its
    // status byte. Interim replies are waited out unless `acceptInterim`.
    Bytes Command(const Query& query, bool acceptInterim = false, int timeoutMs = 30000);
    // Waits for the reply to a command sent earlier (the track upload answers after its data).
    Bytes AwaitReply(bool acceptInterim, int timeoutMs);

    void Acquire();
    void Release();
    Bytes Status();
    bool DiscPresent();
    // The operating state (0xc5ff ready, 0xffff blank disc, 0xff10 no disc, ...).
    int OperatingState();
    int DiscFlags();
    int TrackCount();
    std::string RawDiscTitle();
    std::string TrackTitle(int track);
    void Capacity(int64_t& usedFrames, int64_t& totalFrames, int64_t& leftFrames);
    int64_t TrackFrames(int track);
    void TrackEncoding(int track, int& encoding, int& channels);
    int TrackFlags(int track);
    void EraseDisc();
    void SetDiscTitle(const std::string& title);
    void SetTrackTitle(int track, const std::string& title);

    // Disc state; ReadContents also lists every track (a few commands per track).
    DiscInfo ReadDiscInfo();
    void ReadContents(DiscInfo& info, const std::function<bool()>& cancelled = nullptr);

    // Secure download commands.
    void EnterSecureSession();
    void LeaveSecureSession();
    Bytes LeafId();
    void SendKeyData(uint32_t ekbId, const std::vector<Bytes>& chain, int depth, const Bytes& signature);
    Bytes SessionKeyExchange(const Bytes& hostNonce);
    void SessionKeyForget();
    void SetupDownload(const Bytes& contentId, const Bytes& keyEncryptionKey, const Bytes& sessionKey);
    void DisableNewTrackProtection(int value);
    void CommitTrack(int track, const Bytes& sessionKey);

    Transport& GetTransport() { return fTransport; }
    // Receives every command and reply as hex, for debugging.
    std::function<void(const std::string&)> log;

private:
    int ReplyLength();
    Bytes ReadReply(int timeoutMs);
    void Descriptor(const char* descriptor, const char* action);

    Transport& fTransport;
    Bytes fLastCommand;
};

// Supplies the PCM of one track: 16-bit big-endian stereo at 44.1 kHz.
class PcmSource {
public:
    virtual ~PcmSource() {}
    // Fills up to `length` bytes; returns 0 at the end of the data.
    virtual size_t Read(uint8_t* buffer, size_t length) = 0;
};

struct DownloadProgress {
    uint64_t sent = 0;
    uint64_t total = 0;
};

// Writes one track in SP. Handles the whole per-track sequence: waits for the device, opens a
// secure session with the open-source EKB, sends the encrypted PCM, titles and commits the
// track. `pcmBytes` is the exact data length the source delivers. Returns the new track's
// index. The transfer cannot be interrupted: once the length is announced the device waits
// for all of it.
int DownloadTrack(Device& device, PcmSource& source, uint64_t pcmBytes, const std::string& title,
    const std::function<void(const DownloadProgress&)>& progress = nullptr);

// Leaves a download state behind after an error: closes the secure session and releases.
void Recover(Device& device);

// ---- planning helpers (pure functions, unit tested)

// The retail MAC NetMD derives session keys with: DES-CBC over all but the last block with the
// first key half, then two-key 3DES-CBC over the last block.
Bytes RetailMac(const Bytes& key16, const Bytes& value, const Bytes& iv = Bytes(8, 0));

// A title the device can store in its half-width (ASCII) title area: diacritics dropped,
// typographic punctuation mapped to ASCII, anything else removed; spaces collapsed.
std::string SanitizeTitle(const std::string& utf8);

// The TOC stores titles in 255 cells of 7 characters shared by the disc and all tracks.
const int kTitleCells = 255;
const int kCellChars = 7;
int TitleCells(const std::string& title);
// Shortens the longest titles until everything fits in `availableCells`.
std::vector<std::string> FitTitles(std::vector<std::string> titles, int availableCells);

// Disc frames an SP track of `durationMs` occupies, including an allowance for the track's
// last partly filled cluster.
int64_t SPFramesForDuration(int64_t durationMs);
// Length of the PCM a track of `durationMs` becomes (before padding to a whole frame).
uint64_t PcmBytesForDuration(int64_t durationMs);

std::string FormatFrames(int64_t frames); // "1:02:03" or "4:05"

// Known NetMD devices (USB vendor and product IDs).
struct KnownDevice {
    uint16_t vendor, product;
    const char* name;
};
const KnownDevice* FindKnownDevice(uint16_t vendor, uint16_t product);

} // namespace netmd
} // namespace amp
