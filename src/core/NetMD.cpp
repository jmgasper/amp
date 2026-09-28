#include "NetMD.h"
#include "Des.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>

namespace amp {
namespace netmd {

namespace {

// NetMD status bytes (first byte of a command or reply).
const uint8_t kStatusControl = 0x00;
const uint8_t kStatusNotImplemented = 0x08;
const uint8_t kStatusAccepted = 0x09;
const uint8_t kStatusRejected = 0x0a;
const uint8_t kStatusImplemented = 0x0c;
const uint8_t kStatusInterim = 0x0f;

// Descriptors and the actions that open and close them.
const char* kRootTD = "10 1000";
const char* kAudioContentsTD = "10 1001";
const char* kDiscTitleTD = "10 1801";
const char* kAudioUTOC1TD = "10 1802";
const char* kOperatingStatusBlock = "80 00";
const char* kOpenRead = "01";
const char* kOpenWrite = "03";
const char* kClose = "00";

// The EKB published by the linux-minidisc project, which every NetMD recorder accepts.
const uint32_t kEkbId = 0x26422642;
const uint8_t kEkbRootKey[16] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    0x0f, 0xed, 0xcb, 0xa9, 0x87, 0x65, 0x43, 0x21};
const uint8_t kEkbChain[2][16] = {
    {0x25, 0x45, 0x06, 0x4d, 0xea, 0xca, 0x14, 0xf9, 0x96, 0xbd, 0xc8, 0xa4, 0x06, 0xc2, 0x2b, 0x81},
    {0xfb, 0x60, 0xbd, 0xdd, 0x0d, 0xbc, 0xab, 0x84, 0x8a, 0x00, 0x5e, 0x03, 0x19, 0x4d, 0x3e, 0xda}};
const int kEkbDepth = 9;
const uint8_t kEkbSignature[24] = {0x8f, 0x2b, 0xc3, 0x52, 0xe8, 0x6c, 0x5e, 0xd3, 0x06, 0xdc, 0xae, 0x18,
    0xd2, 0xf3, 0x8c, 0x7f, 0x89, 0xb5, 0xe1, 0x85, 0x55, 0xa1, 0x05, 0xea};
// Content ID and key-encryption key: any value works, these are the customary ones.
const uint8_t kContentId[20] = {0x01, 0x0f, 0x50, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48,
    0xa2, 0x8d, 0x3e, 0x1a, 0x3b, 0x0c, 0x44, 0xaf, 0x2f, 0xa0};
const uint8_t kKeyEncryptionKey[8] = {0x14, 0xe3, 0x83, 0x4e, 0xe2, 0xd3, 0xcc, 0xa5};

const size_t kBulkChunk = 64 * 1024;

int HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

std::string HexDump(const Bytes& data)
{
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (uint8_t b : data) {
        text += digits[b >> 4];
        text += digits[b & 15];
    }
    return text;
}

int FromBcd(unsigned value)
{
    int result = 0, scale = 1;
    while (value) {
        result += (int)(value & 15) * scale;
        value >>= 4;
        scale *= 10;
    }
    return result;
}

Bytes RandomBytes(size_t count)
{
    static std::random_device device;
    Bytes bytes(count);
    for (uint8_t& b : bytes)
        b = (uint8_t)device();
    return bytes;
}

// The operating states a download may start from.
const int kStateReady = 0xc5ff;
const int kStateBlank = 0xffff;

} // namespace

void Transport::Sleep(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ---- Query / Reply

Query& Query::Hex(const char* hex)
{
    int high = -1;
    for (const char* p = hex; *p; p++) {
        int value = HexValue(*p);
        if (value < 0)
            continue;
        if (high < 0)
            high = value;
        else {
            fData.push_back((uint8_t)(high << 4 | value));
            high = -1;
        }
    }
    return *this;
}

Query& Query::U8(unsigned value)
{
    fData.push_back((uint8_t)value);
    return *this;
}

Query& Query::U16(unsigned value)
{
    fData.push_back((uint8_t)(value >> 8));
    fData.push_back((uint8_t)value);
    return *this;
}

Query& Query::U32(uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        fData.push_back((uint8_t)(value >> shift));
    return *this;
}

Query& Query::Append(const Bytes& bytes)
{
    fData.insert(fData.end(), bytes.begin(), bytes.end());
    return *this;
}

void Reply::Need(size_t count) const
{
    if (fPosition + count > fData.size())
        throw Error(Error::kProtocol, "reply too short: " + HexDump(fData));
}

Reply& Reply::Expect(const char* pattern)
{
    const char* p = pattern;
    while (*p) {
        if (*p == ' ') {
            p++;
            continue;
        }
        if (!p[1])
            break;
        Need(1);
        uint8_t actual = fData[fPosition];
        if (!(p[0] == '?' && p[1] == '?')) {
            int expected = HexValue(p[0]) << 4 | HexValue(p[1]);
            if (actual != expected)
                throw Error(Error::kProtocol, std::string("unexpected reply ") + HexDump(fData) + " for " + pattern);
        }
        fPosition++;
        p += 2;
    }
    return *this;
}

uint8_t Reply::U8()
{
    Need(1);
    return fData[fPosition++];
}

uint16_t Reply::U16()
{
    Need(2);
    uint16_t value = (uint16_t)(fData[fPosition] << 8 | fData[fPosition + 1]);
    fPosition += 2;
    return value;
}

uint32_t Reply::U32()
{
    uint32_t high = U16();
    return high << 16 | U16();
}

int Reply::Bcd8()
{
    return FromBcd(U8());
}

int Reply::Bcd16()
{
    return FromBcd(U16());
}

Bytes Reply::Take(size_t count)
{
    Need(count);
    Bytes result(fData.begin() + fPosition, fData.begin() + fPosition + count);
    fPosition += count;
    return result;
}

Bytes Reply::Prefixed16()
{
    return Take(U16());
}

Bytes Reply::Rest()
{
    return Take(Remaining());
}

// ---- Device

int Device::ReplyLength()
{
    uint8_t buffer[4] = {0, 0, 0, 0};
    if (fTransport.VendorIn(0x01, buffer, sizeof(buffer)) < 0)
        throw Error(Error::kIo, "the recorder does not answer");
    return buffer[2];
}

Bytes Device::ReadReply(int timeoutMs)
{
    auto start = std::chrono::steady_clock::now();
    int delay = 5;
    int length;
    while ((length = ReplyLength()) == 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        if (elapsed.count() > timeoutMs)
            throw Error(Error::kTimeout, "the recorder took too long to answer");
        fTransport.Sleep(delay);
        delay = std::min(delay * 2, 200);
    }
    Bytes reply(length);
    ssize_t read = fTransport.VendorIn(0x81, reply.data(), reply.size());
    if (read < 0)
        throw Error(Error::kIo, "cannot read the recorder's reply");
    reply.resize(read);
    ReplyLength();
    return reply;
}

double Device::MeasureLatency(int rounds)
{
    ReplyLength(); // the first transfer may include waking the device up
    std::vector<double> times;
    for (int i = 0; i < rounds; i++) {
        auto start = std::chrono::steady_clock::now();
        ReplyLength();
        times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    // the median: a recorder busy with its disc slows single transfers down
    std::sort(times.begin(), times.end());
    return times.empty() ? 0 : times[times.size() / 2];
}

void Device::Flush()
{
    if (ReplyLength() > 0)
        ReadReply(1000);
}

Bytes Device::Command(const Query& query, bool acceptInterim, int timeoutMs)
{
    Bytes message;
    message.reserve(query.Data().size() + 1);
    message.push_back(kStatusControl);
    message.insert(message.end(), query.Data().begin(), query.Data().end());
    if (log)
        log("> " + HexDump(message));
    if (fTransport.VendorOut(0x80, message.data(), message.size()) < 0)
        throw Error(Error::kIo, "cannot send a command to the recorder");
    fLastCommand = query.Data();
    return AwaitReply(acceptInterim, timeoutMs);
}

Bytes Device::AwaitReply(bool acceptInterim, int timeoutMs)
{
    for (int attempt = 0;; attempt++) {
        Bytes reply = ReadReply(timeoutMs);
        if (log)
            log("< " + HexDump(reply));
        if (reply.empty())
            throw Error(Error::kProtocol, "empty reply");
        uint8_t status = reply[0];
        if (status == kStatusNotImplemented)
            throw Error(Error::kNotImplemented, "command not supported by the recorder");
        if (status == kStatusRejected)
            throw Error(Error::kRejected, "the recorder rejected command " + HexDump(fLastCommand));
        if (status == kStatusInterim && !acceptInterim) {
            if (attempt >= 8)
                throw Error(Error::kTimeout, "the recorder stays busy");
            fTransport.Sleep(100 * attempt);
            continue;
        }
        if (status != kStatusAccepted && status != kStatusImplemented && status != kStatusInterim)
            throw Error(Error::kProtocol, "unknown reply status " + HexDump(reply));
        return Bytes(reply.begin() + 1, reply.end());
    }
}

void Device::Descriptor(const char* descriptor, const char* action)
{
    // the reference implementations ignore failures here, and some devices do fail
    try {
        Command(Query("1808").Hex(descriptor).Hex(action).Hex("00"));
    } catch (const Error& error) {
        if (error.kind() == Error::kIo)
            throw;
    }
}

void Device::Acquire()
{
    Reply(Command(Query("ff 010c ffff ffff ffff ffff ffff ffff"))).Expect("ff 010c ffff ffff ffff ffff ffff ffff");
}

void Device::Release()
{
    Reply(Command(Query("ff 0100 ffff ffff ffff ffff ffff ffff"))).Expect("ff 0100 ffff ffff ffff ffff ffff ffff");
}

Bytes Device::Status()
{
    Descriptor(kOperatingStatusBlock, kOpenRead);
    Reply reply(Command(Query("1809 8001 0230 8800 0030 8804 00 ff00 00000000")));
    reply.Expect("1809 8001 0230 8800 0030 8804 00 1000 00090000");
    Bytes status = reply.Prefixed16();
    Descriptor(kOperatingStatusBlock, kClose);
    return status;
}

bool Device::DiscPresent()
{
    Bytes status = Status();
    return status.size() > 4 && status[4] != 0x80;
}

int Device::OperatingState()
{
    Descriptor(kOperatingStatusBlock, kOpenRead);
    Reply reply(Command(Query("1809 8001 0330 8802 0030 8805 0030 8806 00 ff00 00000000")));
    reply.Expect("1809 8001 0330 ???? ???? ???? ???? ???? ?? 1000 00??0000");
    Bytes data = reply.Prefixed16();
    Descriptor(kOperatingStatusBlock, kClose);
    if (data.size() < 6)
        throw Error(Error::kProtocol, "short playback status");
    return data[4] << 8 | data[5];
}

int Device::DiscFlags()
{
    Descriptor(kRootTD, kOpenRead);
    Reply reply(Command(Query("1806 01101000 ff00 0001000b")));
    reply.Expect("1806 01101000 1000 0001000b");
    int flags = reply.U8();
    Descriptor(kRootTD, kClose);
    return flags;
}

int Device::TrackCount()
{
    Descriptor(kAudioContentsTD, kOpenRead);
    Reply reply(Command(Query("1806 02101001 3000 1000 ff00 00000000")));
    reply.Expect("1806 02101001 ???? ???? 1000 00??0000 0006 0010000200");
    int count = reply.U8();
    Descriptor(kAudioContentsTD, kClose);
    return count;
}

std::string Device::RawDiscTitle()
{
    Descriptor(kAudioContentsTD, kOpenRead);
    Descriptor(kDiscTitleTD, kOpenRead);
    std::string title;
    unsigned done = 0, remaining = 0, total = 1;
    while (done < total) {
        Reply reply(Command(Query("1806 02201801 00 00 3000 0a00 ff00").U16(remaining).U16(done)));
        unsigned chunk;
        if (remaining == 0) {
            reply.Expect("1806 02201801 00?? 3000 0a00 1000");
            chunk = reply.U16();
            reply.Expect("0000 ????000a");
            total = reply.U16();
            if (chunk < 6)
                throw Error(Error::kProtocol, "bad disc title chunk");
            chunk -= 6;
        } else {
            reply.Expect("1806 02201801 00?? 3000 0a00 1000");
            chunk = reply.U16();
            reply.Expect("????");
        }
        Bytes text = reply.Rest();
        title.append(text.begin(), text.end());
        if (chunk == 0)
            break;
        done += chunk;
        remaining = total > done ? total - done : 0;
    }
    Descriptor(kDiscTitleTD, kClose);
    Descriptor(kAudioContentsTD, kClose);
    return title;
}

std::string Device::TrackTitle(int track)
{
    Descriptor(kAudioUTOC1TD, kOpenRead);
    std::string title;
    try {
        Reply reply(Command(Query("1806 022018 02").U16(track).Hex("3000 0a00 ff00 00000000")));
        reply.Expect("1806 022018?? ???? ???? ???? 1000 00??0000 00??000a");
        Bytes text = reply.Prefixed16();
        title.assign(text.begin(), text.end());
    } catch (const Error& error) {
        Descriptor(kAudioUTOC1TD, kClose);
        throw;
    }
    Descriptor(kAudioUTOC1TD, kClose);
    return title;
}

void Device::Capacity(int64_t& usedFrames, int64_t& totalFrames, int64_t& leftFrames)
{
    Descriptor(kRootTD, kOpenRead);
    Reply reply(Command(Query("1806 02101000 3080 0300 ff00 00000000")));
    // the byte before "03" is 80 on Sony recorders and 08 on Panasonic ones
    reply.Expect("1806 02101000 3080 0300 1000 001d0000 001b ??03 0017 8000");
    int64_t values[3];
    for (int64_t& value : values) {
        reply.Expect("0005");
        int hours = reply.Bcd16();
        int minutes = reply.Bcd8();
        int seconds = reply.Bcd8();
        int frames = reply.Bcd8();
        value = ((int64_t)(hours * 60 + minutes) * 60 + seconds) * kFramesPerSecond + frames;
    }
    Descriptor(kRootTD, kClose);
    usedFrames = values[0];
    totalFrames = values[1];
    leftFrames = values[2];
    // some recorders (Sharp) report the time of the current recording mode: bring it back to SP
    while (totalFrames > (int64_t)kFramesPerSecond * 60 * 82) {
        usedFrames /= 2;
        totalFrames /= 2;
        leftFrames /= 2;
    }
}

int64_t Device::TrackFrames(int track)
{
    Descriptor(kAudioContentsTD, kOpenRead);
    Reply reply(Command(Query("1806 02201001").U16(track).Hex("3000 0100 ff00 00000000")));
    reply.Expect("1806 02201001 ???? ???? ???? 1000 00??0000");
    Reply info(reply.Prefixed16());
    Descriptor(kAudioContentsTD, kClose);
    info.Expect("0001 0006 0000");
    int hours = info.Bcd8();
    int minutes = info.Bcd8();
    int seconds = info.Bcd8();
    int frames = info.Bcd8();
    return ((int64_t)(hours * 60 + minutes) * 60 + seconds) * kFramesPerSecond + frames;
}

void Device::TrackEncoding(int track, int& encoding, int& channels)
{
    Descriptor(kAudioContentsTD, kOpenRead);
    Reply reply(Command(Query("1806 02201001").U16(track).Hex("3080 0700 ff00 00000000")));
    reply.Expect("1806 02201001 ???? ???? ???? 1000 00??0000");
    Reply info(reply.Prefixed16());
    Descriptor(kAudioContentsTD, kClose);
    info.Expect("8007 0004 0110");
    encoding = info.U8();
    channels = info.U8();
}

int Device::TrackFlags(int track)
{
    Descriptor(kAudioContentsTD, kOpenRead);
    Reply reply(Command(Query("1806 01201001").U16(track).Hex("ff00 00010008")));
    reply.Expect("1806 01201001 ???? 10 00 00010008");
    int flags = reply.U8();
    Descriptor(kAudioContentsTD, kClose);
    return flags;
}

void Device::EraseDisc()
{
    Reply(Command(Query("1840 ff 0000"), false, 120000)).Expect("1840 00 0000");
}

void Device::SetDiscTitle(const std::string& title)
{
    std::string current = RawDiscTitle();
    if (current == title)
        return; // writing the same title again upsets some recorders
    Descriptor(kDiscTitleTD, kClose);
    Descriptor(kDiscTitleTD, kOpenWrite);
    Query query("1807 02201801 00 00 3000 0a00 5000");
    query.U16((unsigned)title.size()).Hex("0000").U16((unsigned)current.size());
    query.Append(Bytes(title.begin(), title.end()));
    Reply reply(Command(query));
    reply.Expect("1807 02201801 00?? 3000 0a00 5000 ???? 0000 ????");
    Descriptor(kDiscTitleTD, kClose);
    Descriptor(kDiscTitleTD, kOpenRead);
    Descriptor(kDiscTitleTD, kClose);
}

void Device::SetTrackTitle(int track, const std::string& title)
{
    size_t oldLength = 0;
    try {
        std::string current = TrackTitle(track);
        if (current == title)
            return;
        oldLength = current.size();
    } catch (const Error& error) {
        if (error.kind() != Error::kRejected)
            throw;
    }
    Descriptor(kAudioUTOC1TD, kOpenWrite);
    Query query("1807 022018 02");
    query.U16(track).Hex("3000 0a00 5000").U16((unsigned)title.size()).Hex("0000").U16((unsigned)oldLength);
    query.Append(Bytes(title.begin(), title.end()));
    try {
        Reply reply(Command(query));
        reply.Expect("1807 022018?? ???? 3000 0a00 5000 ???? 0000 ????");
    } catch (const Error&) {
        Descriptor(kAudioUTOC1TD, kClose);
        throw;
    }
    Descriptor(kAudioUTOC1TD, kClose);
}

DiscInfo Device::ReadDiscInfo()
{
    DiscInfo info;
    info.present = DiscPresent();
    if (!info.present)
        return info;
    int flags = DiscFlags();
    info.writable = (flags & 0x10) != 0;
    info.writeProtected = (flags & 0x40) != 0;
    info.trackCount = TrackCount();
    Capacity(info.usedFrames, info.totalFrames, info.leftFrames);
    info.rawTitle = RawDiscTitle();
    info.title = info.rawTitle;
    // a disc with groups stores "0;Disc title//1-3;Group//..."
    if (info.title.size() >= 2 && info.title.compare(info.title.size() - 2, 2, "//") == 0) {
        std::string first = info.title.substr(0, info.title.find("//"));
        info.title = first.compare(0, 2, "0;") == 0 ? first.substr(2) : std::string();
    }
    return info;
}

void Device::ReadContents(DiscInfo& info, const std::function<bool()>& cancelled)
{
    info.tracks.clear();
    for (int i = 0; i < info.trackCount; i++) {
        if (cancelled && cancelled())
            return;
        TrackInfo track;
        track.index = i;
        try {
            track.title = TrackTitle(i);
        } catch (const Error& error) {
            if (error.kind() == Error::kIo)
                throw;
        }
        track.frames = TrackFrames(i);
        try {
            int channels = 0;
            TrackEncoding(i, track.encoding, channels);
            track.mono = channels == 0x01;
        } catch (const Error& error) {
            if (error.kind() == Error::kIo)
                throw;
        }
        info.tracks.push_back(track);
    }
}

void Device::EnterSecureSession()
{
    Reply(Command(Query("1800 080046 f0030103 80 ff"))).Expect("1800 080046 f0030103 80 00");
}

void Device::LeaveSecureSession()
{
    Reply(Command(Query("1800 080046 f0030103 81 ff"))).Expect("1800 080046 f0030103 81 00");
}

Bytes Device::LeafId()
{
    Reply reply(Command(Query("1800 080046 f0030103 11 ff")));
    reply.Expect("1800 080046 f0030103 11 00");
    return reply.Rest();
}

void Device::SendKeyData(uint32_t ekbId, const std::vector<Bytes>& chain, int depth, const Bytes& signature)
{
    unsigned length = (unsigned)(16 + 16 * chain.size() + 24);
    Query query("1800 080046 f0030103 12 ff");
    query.U16(length).Hex("0000").U16(length).U32((uint32_t)chain.size()).U32(depth).U32(ekbId).Hex("00000000");
    for (const Bytes& key : chain)
        query.Append(key);
    query.Append(signature);
    Reply(Command(query)).Expect("1800 080046 f0030103 12 01 ???? ????????");
}

Bytes Device::SessionKeyExchange(const Bytes& hostNonce)
{
    Reply reply(Command(Query("1800 080046 f0030103 20 ff 000000").Append(hostNonce)));
    reply.Expect("1800 080046 f0030103 20 ?? 000000");
    return reply.Rest();
}

void Device::SessionKeyForget()
{
    Reply(Command(Query("1800 080046 f0030103 21 ff 000000"))).Expect("1800 080046 f0030103 21 00 000000");
}

void Device::SetupDownload(const Bytes& contentId, const Bytes& keyEncryptionKey, const Bytes& sessionKey)
{
    Bytes message = {1, 1, 1, 1};
    message.insert(message.end(), contentId.begin(), contentId.end());
    message.insert(message.end(), keyEncryptionKey.begin(), keyEncryptionKey.end());
    Bytes encrypted(message.size());
    uint8_t iv[8] = {0};
    Des(sessionKey.data()).EncryptCbc(iv, message.data(), encrypted.data(), message.size());
    Reply(Command(Query("1800 080046 f0030103 22 ff 0000").Append(encrypted))).Expect("1800 080046 f0030103 22 00 0000");
}

void Device::DisableNewTrackProtection(int value)
{
    Reply(Command(Query("1800 080046 f0030103 2b ff").U16(value))).Expect("1800 080046 f0030103 2b 00 ????");
}

void Device::CommitTrack(int track, const Bytes& sessionKey)
{
    uint8_t zero[8] = {0}, authentication[8];
    Des(sessionKey.data()).EncryptEcb(zero, authentication, 8);
    Query query("1800 080046 f0030103 48 ff 00 1001");
    query.U16(track).Append(Bytes(authentication, authentication + 8));
    Reply(Command(query, false, 60000)).Expect("1800 080046 f0030103 48 00 00 1001 ????");
}

// ---- download

namespace {

void WaitUntilReady(Device& device)
{
    for (int i = 0; i < 100; i++) {
        int state = device.OperatingState();
        if (state == kStateReady || state == kStateBlank)
            return;
        device.GetTransport().Sleep(200);
    }
    throw Error(Error::kTimeout, "the recorder is busy");
}

} // namespace

void Recover(Device& device)
{
    try {
        device.SessionKeyForget();
    } catch (const Error&) {
    }
    try {
        device.LeaveSecureSession();
    } catch (const Error&) {
    }
    try {
        device.Release();
    } catch (const Error&) {
    }
}

int DownloadTrack(Device& device, PcmSource& source, uint64_t pcmBytes, const std::string& title,
    const std::function<void(const DownloadProgress&)>& progress)
{
    Transport& transport = device.GetTransport();
    WaitUntilReady(device);
    try {
        device.SessionKeyForget();
        device.LeaveSecureSession();
    } catch (const Error& error) {
        if (error.kind() == Error::kIo)
            throw;
    }
    device.Acquire();
    try {
        device.DisableNewTrackProtection(1);
    } catch (const Error& error) {
        if (error.kind() == Error::kIo)
            throw;
    }

    // session key: a retail MAC of both nonces under the EKB root key
    device.EnterSecureSession();
    device.LeafId();
    std::vector<Bytes> chain;
    for (const auto& key : kEkbChain)
        chain.push_back(Bytes(key, key + 16));
    device.SendKeyData(kEkbId, chain, kEkbDepth, Bytes(kEkbSignature, kEkbSignature + 24));
    Bytes hostNonce = RandomBytes(8);
    Bytes deviceNonce = device.SessionKeyExchange(hostNonce);
    Bytes nonces = hostNonce;
    nonces.insert(nonces.end(), deviceNonce.begin(), deviceNonce.end());
    Bytes sessionKey = RetailMac(Bytes(kEkbRootKey, kEkbRootKey + 16), nonces);
    Bytes kek(kKeyEncryptionKey, kKeyEncryptionKey + 8);
    device.SetupDownload(Bytes(kContentId, kContentId + 20), kek, sessionKey);

    // The data is DES-CBC encrypted with a random key; the device learns it as E(kek, key'),
    // so the packet header carries key' = D(kek, key).
    uint64_t total = (pcmBytes + kPcmFrameSize - 1) / kPcmFrameSize * kPcmFrameSize;
    Bytes dataKey = RandomBytes(8);
    uint8_t headerKey[8];
    Des(kek.data()).DecryptEcb(dataKey.data(), headerKey, 8);
    Des cipher(dataKey.data());
    uint8_t iv[8] = {0};

    transport.Sleep(200);
    Query send("1800 080046 f0030103 28 ff 000100 1001 ffff 00");
    send.U8(kWirePCM).U8(kDiscSPStereo).U32((uint32_t)(total / kPcmFrameSize)).U32((uint32_t)(total + 24));
    Reply(device.Command(send, true)).Expect("1800 080046 f0030103 28 00 000100 1001 ???? 00");
    transport.Sleep(200);

    Bytes chunk(kBulkChunk);
    DownloadProgress state;
    state.total = total;
    uint64_t delivered = 0;
    bool first = true;
    while (state.sent < total) {
        size_t header = first ? 24 : 0;
        // the header shares the first transfer, which stays at kBulkChunk like every other
        size_t length = (size_t)std::min<uint64_t>(kBulkChunk - header, total - state.sent);
        uint8_t* data = chunk.data() + header;
        size_t filled = 0;
        while (filled < length && delivered < pcmBytes) {
            size_t want = (size_t)std::min<uint64_t>(length - filled, pcmBytes - delivered);
            size_t got = source.Read(data + filled, want);
            if (got == 0)
                break; // a short source is padded with silence
            filled += got;
            delivered += got;
        }
        memset(data + filled, 0, length - filled);
        cipher.EncryptCbc(iv, data, data, length);
        if (first) {
            memset(chunk.data(), 0, 4);
            for (int i = 0; i < 4; i++)
                chunk[4 + i] = (uint8_t)(total >> (24 - 8 * i));
            memcpy(chunk.data() + 8, headerKey, 8);
            memset(chunk.data() + 16, 0, 8); // the first IV
            first = false;
        }
        if (device.log && (first || state.sent == 0))
            device.log("bulk " + std::to_string(header + length) + " bytes of " + std::to_string(total + 24));
        ssize_t written = transport.BulkOut(chunk.data(), header + length);
        if (written != (ssize_t)(header + length))
            throw Error(Error::kIo, "the transfer to the recorder failed");
        state.sent += length;
        if (progress)
            progress(state);
    }

    // the device answers the upload command once the track is on the disc
    Reply reply(device.AwaitReply(false, 600000));
    reply.Expect("1800 080046 f0030103 28 00 000100 1001");
    int track = reply.U16();
    // the rest (encrypted track UUID and content ID) only matters for check-in

    if (!title.empty()) {
        try {
            device.SetTrackTitle(track, title);
        } catch (const Error& error) {
            if (error.kind() == Error::kIo)
                throw; // a full title table is not worth losing the track over
        }
    }
    device.CommitTrack(track, sessionKey);
    try {
        device.SessionKeyForget();
    } catch (const Error& error) {
        if (error.kind() == Error::kIo)
            throw;
    }
    device.LeaveSecureSession();
    device.Release();
    return track;
}

// ---- planning helpers

Bytes RetailMac(const Bytes& key16, const Bytes& value, const Bytes& iv)
{
    Bytes chain(iv);
    size_t head = value.size() - 8;
    Bytes scratch(head);
    if (head > 0)
        Des(key16.data()).EncryptCbc(chain.data(), value.data(), scratch.data(), head);
    Bytes mac(8);
    TripleDes(key16.data()).EncryptCbc(chain.data(), value.data() + head, mac.data(), 8);
    return mac;
}

namespace {

// U+00C0..U+00FF without their diacritics.
const char* const kLatin1[64] = {
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"};
// U+0100..U+017F; '?' marks the four ligatures handled separately.
const char kLatinExtendedA[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIi??JjKkkLlLlLlLlLlNnNnNnnNnOoOoOo??RrRrRr"
    "SsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

// Decodes one UTF-8 sequence; invalid bytes come back as U+FFFD.
uint32_t NextCodePoint(const std::string& text, size_t& i)
{
    uint8_t c = (uint8_t)text[i++];
    if (c < 0x80)
        return c;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
    if (extra < 0)
        return 0xfffd;
    uint32_t value = c & (0x3f >> extra);
    for (int k = 0; k < extra; k++) {
        if (i >= text.size() || ((uint8_t)text[i] & 0xc0) != 0x80)
            return 0xfffd;
        value = value << 6 | ((uint8_t)text[i++] & 0x3f);
    }
    return value;
}

// The ASCII a code point is written as, or "" when it has none.
std::string AsciiFor(uint32_t c)
{
    if (c == '\\')
        return "/";  // 0x5c and 0x7e are the yen sign and an overline in the device's Shift JIS
    if (c == '~')
        return "-";
    if (c >= 0x20 && c < 0x7f)
        return std::string(1, (char)c);
    if (c == '\t' || c == '\n' || c == '\r' || c == 0xa0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200a))
        return " ";
    if (c >= 0xc0 && c <= 0xff)
        return kLatin1[c - 0xc0];
    if (c >= 0x100 && c <= 0x17f) {
        switch (c) {
            case 0x132: return "IJ";
            case 0x133: return "ij";
            case 0x152: return "OE";
            case 0x153: return "oe";
        }
        return std::string(1, kLatinExtendedA[c - 0x100]);
    }
    if (c >= 0xff01 && c <= 0xff5e)
        return AsciiFor(c - 0xfee0); // full-width forms
    switch (c) {
        case 0xa1: return "!";
        case 0xbf: return "?";
        case 0xab: case 0xbb: case 0x201c: case 0x201d: case 0x201e: case 0x2033: return "\"";
        case 0xb4: case 0x2018: case 0x2019: case 0x201a: case 0x2032: return "'";
        case 0xb7: case 0x2022: case 0x30fb: return ".";
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";
        case 0x2026: return "...";
        case 0x2044: return "/";
        case 0x266f: return "#";
    }
    return ""; // combining marks, CJK and everything else the device cannot show
}

} // namespace

std::string SanitizeTitle(const std::string& utf8)
{
    std::string result;
    size_t i = 0;
    while (i < utf8.size()) {
        std::string ascii = AsciiFor(NextCodePoint(utf8, i));
        for (char c : ascii) {
            if (c == ' ' && (result.empty() || result.back() == ' '))
                continue;
            result += c;
        }
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

int TitleCells(const std::string& title)
{
    return (int)((title.size() + kCellChars - 1) / kCellChars);
}

std::vector<std::string> FitTitles(std::vector<std::string> titles, int availableCells)
{
    auto cellsWithLimit = [&](size_t limit) {
        int cells = 0;
        for (const std::string& title : titles)
            cells += TitleCells(title.substr(0, limit));
        return cells;
    };
    size_t longest = 0;
    for (const std::string& title : titles)
        longest = std::max(longest, title.size());
    if (cellsWithLimit(longest) <= availableCells)
        return titles;
    // the longest length every title may keep; below one cell each, number titles in order
    size_t low = 0, high = longest;
    while (low < high) {
        size_t middle = (low + high + 1) / 2;
        if (cellsWithLimit(middle) <= availableCells)
            low = middle;
        else
            high = middle - 1;
    }
    if (low < (size_t)kCellChars) {
        int left = std::max(availableCells, 0);
        for (std::string& title : titles) {
            if (title.empty())
                continue;
            if (left > 0) {
                title = title.substr(0, kCellChars);
                left--;
            } else
                title.clear();
        }
        return titles;
    }
    for (std::string& title : titles) {
        if (title.size() > low) {
            title.resize(low);
            while (!title.empty() && title.back() == ' ')
                title.pop_back();
        }
    }
    return titles;
}

int64_t SPFramesForDuration(int64_t durationMs)
{
    // A track occupies whole clusters (176 SP sound groups of 512 samples, 2.0434 s) plus one
    // more; measured on an MZ-NE410-type recorder, five songs of 3:05 took 3:22 of disc.
    const int64_t clusterSamples = 176 * 512;
    int64_t samples = (durationMs * 44100 + 999) / 1000;
    int64_t clusters = (samples + clusterSamples - 1) / clusterSamples + 1;
    return clusters * clusterSamples * kFramesPerSecond / 44100;
}

uint64_t PcmBytesForDuration(int64_t durationMs)
{
    return (uint64_t)(durationMs * 44100 / 1000) * 4;
}

std::string FormatFrames(int64_t frames)
{
    int64_t seconds = std::max<int64_t>(frames, 0) / kFramesPerSecond;
    char text[32];
    if (seconds >= 3600)
        snprintf(text, sizeof(text), "%d:%02d:%02d", (int)(seconds / 3600), (int)(seconds / 60 % 60), (int)(seconds % 60));
    else
        snprintf(text, sizeof(text), "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
    return text;
}

namespace {

// USB IDs of the NetMD recorders known to the linux-minidisc and netmd-js projects.
const KnownDevice kKnownDevices[] = {
    {0x04dd, 0x7202, "Sharp IM-MT899H"},
    {0x04dd, 0x9013, "Sharp IM-DR400"},
    {0x04dd, 0x9014, "Sharp IM-DR80"},
    {0x054c, 0x0034, "Sony PCLK-XX"},
    {0x054c, 0x0036, "Sony NetMD"},
    {0x054c, 0x0075, "Sony MZ-N1"},
    {0x054c, 0x007c, "Sony NetMD"},
    {0x054c, 0x0080, "Sony LAM-1"},
    {0x054c, 0x0081, "Sony MDS-JB980/MDS-NT1/MDS-JE780"},
    {0x054c, 0x0084, "Sony MZ-N505"},
    {0x054c, 0x0085, "Sony MZ-S1"},
    {0x054c, 0x0086, "Sony MZ-N707"},
    {0x054c, 0x008e, "Sony CMT-C7NT"},
    {0x054c, 0x0097, "Sony PCGA-MDN1"},
    {0x054c, 0x00ad, "Sony CMT-L7HD"},
    {0x054c, 0x00c6, "Sony MZ-N10"},
    {0x054c, 0x00c7, "Sony MZ-N910"},
    {0x054c, 0x00c8, "Sony MZ-N710/NF810"},
    {0x054c, 0x00c9, "Sony MZ-N510/N610"},
    {0x054c, 0x00ca, "Sony MZ-NE410/DN430/NF520D"},
    {0x054c, 0x00e7, "Sony CMT-M333NT/M373NT"},
    {0x054c, 0x00eb, "Sony MZ-NE810/NE910"},
    {0x054c, 0x0101, "Sony LAM"},
    {0x054c, 0x0113, "Aiwa AM-NX1"},
    {0x054c, 0x011a, "Sony CMT-SE7"},
    {0x054c, 0x013f, "Sony MDS-S500"},
    {0x054c, 0x0148, "Sony MDS-A1"},
    {0x054c, 0x014c, "Aiwa AM-NX9"},
    {0x054c, 0x017e, "Sony MZ-NH1"},
    {0x054c, 0x0180, "Sony MZ-NH3D"},
    {0x054c, 0x0182, "Sony MZ-NH900"},
    {0x054c, 0x0184, "Sony MZ-NH700/NH800"},
    {0x054c, 0x0186, "Sony MZ-NH600"},
    {0x054c, 0x0187, "Sony MZ-NH600D"},
    {0x054c, 0x0188, "Sony MZ-N920"},
    {0x054c, 0x018a, "Sony LAM-3"},
    {0x054c, 0x01e9, "Sony MZ-DH10P"},
    {0x054c, 0x0219, "Sony MZ-RH10"},
    {0x054c, 0x021b, "Sony MZ-RH710/MZ-RH910"},
    {0x054c, 0x021d, "Sony CMT-AH10"},
    {0x054c, 0x022c, "Sony CMT-AH10"},
    {0x054c, 0x023c, "Sony DS-HMD1"},
    {0x054c, 0x0286, "Sony MZ-RH1"},
    {0x0b28, 0x1004, "Kenwood MDX-J9"},
    {0x04da, 0x23b3, "Panasonic SJ-MR250"},
    {0x04da, 0x23b6, "Panasonic SJ-MR270"},
    {0x0411, 0x0083, "Buffalo MD-HUSB"},
};

} // namespace

const KnownDevice* FindKnownDevice(uint16_t vendor, uint16_t product)
{
    for (const KnownDevice& device : kKnownDevices)
        if (device.vendor == vendor && device.product == product)
            return &device;
    return nullptr;
}

} // namespace netmd
} // namespace amp
