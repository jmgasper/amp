// Unit tests for Amp's portable core. Build and run with `make check` (on Haiku) or compile
// this file with src/core on any system with a C++17 compiler.
#include "core/Des.h"
#include "core/NetMD.h"
#include "core/NetMDSimulator.h"
#include "core/Resampler.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace amp;

namespace {

int gFailures = 0;

void Check(bool condition, const char* what, int line)
{
    if (!condition) {
        fprintf(stderr, "FAILED (line %d): %s\n", line, what);
        gFailures++;
    }
}

#define CHECK(condition) Check((condition), #condition, __LINE__)

netmd::Bytes Hex(const char* text)
{
    return netmd::Query(text).Data();
}

std::string ToHex(const netmd::Bytes& bytes)
{
    std::string text;
    char digits[3];
    for (uint8_t b : bytes) {
        snprintf(digits, sizeof(digits), "%02x", b);
        text += digits;
    }
    return text;
}

// Serves generated PCM without a file.
class ToneSource : public netmd::PcmSource {
public:
    explicit ToneSource(uint64_t bytes) : fLeft(bytes) {}
    size_t Read(uint8_t* buffer, size_t length) override
    {
        size_t count = (size_t)std::min<uint64_t>(length, fLeft);
        for (size_t i = 0; i < count; i++)
            buffer[i] = (uint8_t)(fPosition++ * 7);
        fLeft -= count;
        return count;
    }

private:
    uint64_t fLeft;
    uint64_t fPosition = 0;
};

void TestDes()
{
    netmd::Bytes key = Hex("133457799BBCDFF1");
    netmd::Bytes plain = Hex("0123456789ABCDEF");
    uint8_t out[8];
    Des(key.data()).EncryptEcb(plain.data(), out, 8);
    CHECK(ToHex(netmd::Bytes(out, out + 8)) == "85e813540f0ab405");
    uint8_t back[8];
    Des(key.data()).DecryptEcb(out, back, 8);
    CHECK(memcmp(back, plain.data(), 8) == 0);

    netmd::Bytes mac = netmd::RetailMac(Hex("123456789abcdef00fedcba987654321"), Hex("0011223344556677 8899aabbccddeeff"));
    CHECK(ToHex(mac) == "d1747712a78fe4b0");
}

void TestTitles()
{
    CHECK(netmd::SanitizeTitle("Bj\xc3\xb6rk \xe2\x80\x93 J\xc3\xb3ga") == "Bjork - Joga");
    CHECK(netmd::SanitizeTitle("  a  \\ b ~ ") == "a / b -");
    CHECK(netmd::SanitizeTitle("\xe6\x9d\xb1\xe4\xba\xac") == "");
    CHECK(netmd::TitleCells("") == 0);
    CHECK(netmd::TitleCells("1234567") == 1);
    CHECK(netmd::TitleCells("12345678") == 2);
    std::vector<std::string> titles(30, std::string(40, 'x'));
    std::vector<std::string> fitted = netmd::FitTitles(titles, 100);
    int cells = 0;
    for (const std::string& title : fitted)
        cells += netmd::TitleCells(title);
    CHECK(cells <= 100);
    CHECK(fitted[0].size() >= 14); // shortened evenly, not dropped
    fitted = netmd::FitTitles(titles, 10);
    int kept = 0;
    for (const std::string& title : fitted)
        kept += !title.empty();
    CHECK(kept == 10);
    CHECK(netmd::FormatFrames(512 * 65) == "1:05");
    CHECK(netmd::FormatFrames((int64_t)512 * 3725) == "1:02:05");
}

void TestResampler()
{
    Resampler resampler(48000, 44100, 2);
    std::vector<float> input(48000 * 2), output;
    for (size_t i = 0; i < 48000; i++)
        input[i * 2] = input[i * 2 + 1] = 0.5f * (float)sin(2 * M_PI * 1000 * i / 48000.0);
    for (size_t i = 0; i < 48000; i += 1000)
        resampler.Process(&input[i * 2], 1000, output);
    resampler.Flush(output);
    CHECK(output.size() == 44100 * 2);
    float peak = 0;
    for (size_t i = 4410 * 2; i < output.size() - 4410 * 2; i++)
        peak = std::max(peak, std::fabs(output[i]));
    CHECK(std::fabs(peak - 0.5f) < 0.001f);
}

void TestProtocol()
{
    netmd::Simulator simulator;
    simulator.discTitle = "Old Disc";
    simulator.tracks.push_back({"Old Song", 512 * 200, netmd::kEncodingLP2});
    netmd::Device device(simulator);
    device.Flush();
    netmd::DiscInfo info = device.ReadDiscInfo();
    CHECK(info.present);
    CHECK(info.writable);
    CHECK(info.title == "Old Disc");
    CHECK(info.trackCount == 1);
    CHECK(info.totalFrames == (int64_t)80 * 60 * 512);
    CHECK(info.leftFrames == info.totalFrames - 512 * 100); // LP2 takes half the SP space
    device.ReadContents(info);
    CHECK(info.tracks.size() == 1 && info.tracks[0].title == "Old Song");
    CHECK(info.tracks[0].encoding == netmd::kEncodingLP2);

    device.EraseDisc();
    device.SetDiscTitle("New Disc");
    uint64_t bytes = (uint64_t)netmd::kPcmBytesPerSecond * 3 + 1000; // not a whole frame
    ToneSource source(bytes);
    uint64_t lastSent = 0;
    int track = netmd::DownloadTrack(device, source, bytes, "First Song",
        [&](const netmd::DownloadProgress& progress) { lastSent = progress.sent; });
    CHECK(track == 0);
    CHECK(lastSent % netmd::kPcmFrameSize == 0 && lastSent >= bytes);
    info = device.ReadDiscInfo();
    device.ReadContents(info);
    CHECK(info.title == "New Disc");
    CHECK(info.trackCount == 1);
    CHECK(info.tracks.size() == 1 && info.tracks[0].title == "First Song");
    CHECK(info.tracks[0].frames >= 512 * 3);

    // groups: the disc title is the part before the first "//"
    device.SetDiscTitle("0;Mix Tape//1;Side A//");
    info = device.ReadDiscInfo();
    CHECK(info.title == "Mix Tape");
    CHECK(info.rawTitle == "0;Mix Tape//1;Side A//");

    // a write-protected disc refuses the upload
    simulator.writeProtected = true;
    info = device.ReadDiscInfo();
    CHECK(info.writeProtected);
    bool rejected = false;
    try {
        ToneSource more(bytes);
        netmd::DownloadTrack(device, more, bytes, "Nope");
    } catch (const netmd::Error& error) {
        rejected = error.kind() == netmd::Error::kRejected;
        netmd::Recover(device);
    }
    CHECK(rejected);
}

} // namespace

int main()
{
    TestDes();
    TestTitles();
    TestResampler();
    TestProtocol();
    if (gFailures) {
        fprintf(stderr, "%d check(s) failed\n", gFailures);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
