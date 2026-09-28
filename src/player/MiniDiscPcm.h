// Turns local audio files into the PCM a NetMD recorder records in SP: 44.1 kHz, 16-bit,
// stereo, big-endian. Any format the Media Kit decodes works; other rates are resampled.
#pragma once
#include "core/NetMD.h"
#include <cstdio>
#include <functional>
#include <string>

namespace amp {

// Decodes `path` into the raw PCM file `outputPath`. Returns the byte count, or -1 with
// `error` set. `progress` receives the decoded fraction and may return false to cancel.
int64_t ConvertToMiniDiscPcm(const std::string& path, const std::string& outputPath,
    const std::function<bool(float)>& progress, std::string& error);

// Feeds a converted file to netmd::DownloadTrack.
class PcmFileSource : public netmd::PcmSource {
public:
    explicit PcmFileSource(const std::string& path);
    ~PcmFileSource() override;
    bool IsOpen() const { return fFile != nullptr; }
    size_t Read(uint8_t* buffer, size_t length) override;

private:
    FILE* fFile;
};

} // namespace amp
