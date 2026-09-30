#include "TagStream.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace amp {

namespace {
const size_t kHead = 64 * 1024;      // tags sit at the start of nearly every format
const size_t kTail = 4 * 1024;       // ID3v1 and APE footers
const size_t kJump = 8 * 1024;       // a seek into the middle: usually one atom or frame header
const size_t kLargest = 256 * 1024;  // limit for read-ahead, not for what a caller asks for
}

TagStream::TagStream(const std::string& path)
    : fPath(path)
{
    fFile = open(path.c_str(), O_RDONLY);
    struct stat st;
    if (fFile >= 0 && fstat(fFile, &st) == 0)
        fLength = st.st_size;
}

TagStream::~TagStream()
{
    if (fFile >= 0)
        close(fFile);
}

void TagStream::seek(TagLib::offset_t offset, Position position)
{
    if (position == Beginning)
        fPosition = offset;
    else if (position == Current)
        fPosition += offset;
    else
        fPosition = fLength + offset;
    if (fPosition < 0)
        fPosition = 0;
}

const std::string* TagStream::PieceAt(off_t position, size_t wanted, off_t& start)
{
    if (fFile < 0 || position >= fLength)
        return nullptr;
    auto next = fPieces.upper_bound(position);
    off_t previousEnd = 0;
    if (next != fPieces.begin()) {
        auto found = std::prev(next);
        previousEnd = found->first + (off_t)found->second.size();
        if (position < previousEnd) {
            start = found->first;
            return &found->second;
        }
    }
    // how much to fetch beyond what was asked for
    size_t ahead = kJump;
    if (position == 0)
        ahead = kHead;
    else if (position == fLastEnd)
        ahead = std::min(kLargest, std::max(kJump, fLastSize * 2));
    off_t from = position;
    if (position >= fLength - (off_t)kTail) {
        // the footers are read back to front: take the whole tail at once
        from = std::max(previousEnd, fLength - (off_t)kTail);
        ahead = kTail;
    }
    off_t to = std::min<off_t>(fLength, std::max(position + (off_t)wanted, from + (off_t)ahead));
    if (next != fPieces.end())
        to = std::min(to, next->first);
    std::string data((size_t)(to - from), 0);
    size_t done = 0;
    while (done < data.size()) {
        ssize_t got = pread(fFile, &data[done], data.size() - done, from + (off_t)done);
        fReads++;
        if (got <= 0)
            break;
        done += (size_t)got;
        fBytes += got;
    }
    data.resize(done);
    if (from + (off_t)done <= position)
        return nullptr;
    fLastEnd = from + (off_t)done;
    fLastSize = done;
    start = from;
    return &(fPieces[from] = std::move(data));
}

void TagStream::SetLength(off_t length)
{
    fLength = std::max<off_t>(0, std::min(length, fLength));
}

TagLib::ByteVector TagStream::readBlock(size_t length)
{
    if (fPosition >= fLength || length == 0)
        return TagLib::ByteVector();
    length = (size_t)std::min<off_t>((off_t)length, fLength - fPosition);
    TagLib::ByteVector block((unsigned int)length, 0);
    size_t done = 0;
    while (done < length) {
        off_t start = 0;
        const std::string* piece = PieceAt(fPosition + (off_t)done, length - done, start);
        if (!piece)
            break;
        size_t offset = (size_t)(fPosition + (off_t)done - start);
        size_t count = std::min(length - done, piece->size() - offset);
        memcpy(block.data() + done, piece->data() + offset, count);
        done += count;
    }
    block.resize((unsigned int)done);
    fPosition += (off_t)done;
    return block;
}

// ---- MP4 --------------------------------------------------------------------

namespace {

struct Box {
    off_t start = 0;
    off_t body = 0;   // first byte after the header
    off_t end = 0;
    char type[5] = {0, 0, 0, 0, 0};
};

uint64_t BigEndian(const TagLib::ByteVector& bytes, size_t offset, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; i++)
        value = (value << 8) | (uint8_t)bytes[(unsigned int)(offset + i)];
    return value;
}

bool ReadBox(TagStream& stream, off_t position, off_t limit, Box& box)
{
    if (position + 8 > limit)
        return false;
    stream.seek(position);
    TagLib::ByteVector header = stream.readBlock(16);
    if (header.size() < 8)
        return false;
    uint64_t size = BigEndian(header, 0, 4);
    memcpy(box.type, header.data() + 4, 4);
    box.start = position;
    box.body = position + 8;
    if (size == 1) {
        if (header.size() < 16)
            return false;
        size = BigEndian(header, 8, 8);
        box.body = position + 16;
    } else if (size == 0)
        size = (uint64_t)(limit - position);
    if (size < (uint64_t)(box.body - position) || size > (uint64_t)(limit - position))
        return false;
    box.end = position + (off_t)size;
    return true;
}

struct MoovFacts {
    uint64_t timescale = 0, duration = 0, fragmentDuration = 0;
    std::string codec;
};

void ReadMoov(TagStream& stream, off_t from, off_t to, int depth, MoovFacts& facts)
{
    Box box;
    int count = 0;
    for (off_t position = from; count < 64 && ReadBox(stream, position, to, box); position = box.end, count++) {
        if (!strcmp(box.type, "mvhd") || !strcmp(box.type, "mehd") || !strcmp(box.type, "stsd")) {
            stream.seek(box.body);
            TagLib::ByteVector body = stream.readBlock((size_t)std::min<off_t>(32, box.end - box.body));
            bool wide = !body.isEmpty() && body[0] == 1;
            if (!strcmp(box.type, "mvhd") && body.size() >= (wide ? 32u : 20u)) {
                facts.timescale = BigEndian(body, wide ? 20 : 12, 4);
                facts.duration = BigEndian(body, wide ? 24 : 16, wide ? 8 : 4);
                if (facts.duration == (wide ? UINT64_MAX : 0xffffffffu))
                    facts.duration = 0; // "unknown"
            } else if (!strcmp(box.type, "mehd") && body.size() >= (wide ? 12u : 8u)) {
                facts.fragmentDuration = BigEndian(body, 4, wide ? 8 : 4);
            } else if (!strcmp(box.type, "stsd") && body.size() >= 16 && facts.codec.empty()) {
                facts.codec.assign(body.data() + 12, 4);
            }
        } else if (depth < 6 && (!strcmp(box.type, "trak") || !strcmp(box.type, "mdia") || !strcmp(box.type, "minf")
                || !strcmp(box.type, "stbl") || !strcmp(box.type, "mvex"))) {
            ReadMoov(stream, box.body, box.end, depth + 1, facts);
        }
    }
}

} // namespace

bool ReadMp4Info(TagStream& stream, Mp4Info& info)
{
    off_t length = stream.length();
    Box box;
    int count = 0;
    bool found = false;
    for (off_t position = 0; count < 64 && ReadBox(stream, position, length, box); position = box.end, count++) {
        if (found || !strcmp(box.type, "moof")) {
            info.fragmented = !strcmp(box.type, "moof");
            break;
        }
        if (strcmp(box.type, "moov") != 0)
            continue;
        found = true;
        info.moovEnd = box.end;
        MoovFacts facts;
        ReadMoov(stream, box.body, box.end, 0, facts);
        info.codec = facts.codec;
        uint64_t duration = facts.duration ? facts.duration : facts.fragmentDuration;
        if (facts.timescale > 0 && duration > 0)
            info.durationMs = (int64_t)(duration * 1000.0 / facts.timescale + 0.5);
    }
    stream.seek(0);
    return found;
}

} // namespace amp
