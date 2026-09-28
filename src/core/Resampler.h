// Band-limited sample rate conversion for interleaved float audio: a Kaiser-windowed sinc,
// tabulated once and interpolated per output sample.
#pragma once
#include <cstddef>
#include <vector>

namespace amp {

class Resampler {
public:
    Resampler(int inputRate, int outputRate, int channels);

    // Appends the output for `frames` interleaved input frames to `output`.
    void Process(const float* input, size_t frames, std::vector<float>& output);
    // Appends the output still owed once the input has ended.
    void Flush(std::vector<float>& output);

private:
    void Produce(std::vector<float>& output, bool final);
    float Kernel(double x) const;

    int fChannels;
    double fStep;                  // input frames per output frame
    int fHalfWidth;                // filter half width in input frames
    std::vector<float> fTable;     // kernel at kTableResolution points per input frame
    std::vector<float> fBuffer;    // input frames not yet fully used, interleaved
    long long fBufferStart;        // input index of fBuffer's first frame
    long long fInputFrames = 0;    // input frames received so far
    long long fOutputFrames = 0;   // output frames produced so far
};

} // namespace amp
