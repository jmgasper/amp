// Read-only file access for TagLib that fetches a file in few, well-chosen pieces.
// TagLib's own stream reads a kilobyte at a time and seeks a lot; on a network share every
// one of those reads is a round trip to the server.
#pragma once
#include <map>
#include <string>
#include <sys/types.h>
#include <taglib/tiostream.h>

namespace amp {

class TagStream : public TagLib::IOStream {
public:
    explicit TagStream(const std::string& path);
    ~TagStream() override;

    TagLib::FileName name() const override { return fPath.c_str(); }
    TagLib::ByteVector readBlock(size_t length) override;
    void writeBlock(const TagLib::ByteVector&) override {}
    void insert(const TagLib::ByteVector&, TagLib::offset_t, size_t) override {}
    void removeBlock(TagLib::offset_t, size_t) override {}
    bool readOnly() const override { return true; }
    bool isOpen() const override { return fFile >= 0; }
    void seek(TagLib::offset_t offset, Position position = Beginning) override;
    void clear() override {}
    TagLib::offset_t tell() const override { return fPosition; }
    TagLib::offset_t length() override { return fLength; }
    void truncate(TagLib::offset_t) override {}

    // Lets the file end at `length` for everybody who reads it from now on.
    void SetLength(off_t length);

    // How many reads went to the file, and how many bytes they returned.
    int ReadCount() const { return fReads; }
    off_t BytesRead() const { return fBytes; }

private:
    // The piece holding `position`, fetched when it is not there yet; nullptr at the end of
    // the file or when reading fails. `wanted` is what the caller still needs from there.
    const std::string* PieceAt(off_t position, size_t wanted, off_t& start);

    std::string fPath;
    int fFile = -1;
    off_t fLength = 0;
    off_t fPosition = 0;
    std::map<off_t, std::string> fPieces; // by offset, never overlapping
    off_t fLastEnd = -1;                  // where the last fetched piece ended
    size_t fLastSize = 0;
    int fReads = 0;
    off_t fBytes = 0;
};

// What an MP4 file says about itself in its "moov" box.
struct Mp4Info {
    bool fragmented = false;   // the sound follows in "moof" fragments
    off_t moovEnd = 0;         // where the moov box ends
    int64_t durationMs = 0;    // 0 when the file does not say
    std::string codec;         // sample entry of the first track: "mp4a", "alac", "fLaC" ...
};

// Reads the boxes of an MP4 file up to its moov box. A fragmented file keeps its length in
// the "mehd" box, which TagLib does not read; it would instead visit each of the hundreds of
// fragments, one read from the server for every one of them. False when there is no moov box.
bool ReadMp4Info(TagStream& stream, Mp4Info& info);

} // namespace amp
