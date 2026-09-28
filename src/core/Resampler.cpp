#include "Resampler.h"
#include <algorithm>
#include <cmath>

namespace amp {

namespace {

const int kTableResolution = 512; // kernel points per input frame
const int kZeroCrossings = 32;    // on each side of the kernel
const double kKaiserBeta = 8.6;   // about 90 dB of stopband rejection

double BesselI0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 40; k++) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < sum * 1e-12)
            break;
    }
    return sum;
}

} // namespace

Resampler::Resampler(int inputRate, int outputRate, int channels)
    : fChannels(channels),
      fStep((double)inputRate / outputRate)
{
    // cutoff just below the lower of the two Nyquist frequencies, in cycles per input frame
    double cutoff = 0.5 * std::min(1.0, (double)outputRate / inputRate) * 0.96;
    fHalfWidth = (int)std::ceil(kZeroCrossings / (2 * cutoff));
    fTable.resize((size_t)fHalfWidth * kTableResolution + 2);
    double norm = BesselI0(kKaiserBeta);
    for (size_t i = 0; i < fTable.size(); i++) {
        double x = (double)i / kTableResolution;
        double r = x / fHalfWidth;
        if (r >= 1) {
            fTable[i] = 0;
            continue;
        }
        double arg = 2 * cutoff * x * M_PI;
        double sinc = x == 0 ? 1 : std::sin(arg) / arg;
        double window = BesselI0(kKaiserBeta * std::sqrt(1 - r * r)) / norm;
        fTable[i] = (float)(2 * cutoff * sinc * window);
    }
    // silence before the first sample, so the first outputs see a full kernel
    fBufferStart = -fHalfWidth;
    fBuffer.assign((size_t)fHalfWidth * fChannels, 0.0f);
}

float Resampler::Kernel(double x) const
{
    x = std::fabs(x) * kTableResolution;
    size_t index = (size_t)x;
    if (index + 1 >= fTable.size())
        return 0;
    float fraction = (float)(x - index);
    return fTable[index] + (fTable[index + 1] - fTable[index]) * fraction;
}

void Resampler::Process(const float* input, size_t frames, std::vector<float>& output)
{
    fBuffer.insert(fBuffer.end(), input, input + frames * fChannels);
    fInputFrames += frames;
    Produce(output, false);
}

void Resampler::Flush(std::vector<float>& output)
{
    fBuffer.insert(fBuffer.end(), (size_t)fHalfWidth * fChannels, 0.0f);
    Produce(output, true);
}

void Resampler::Produce(std::vector<float>& output, bool final)
{
    long long bufferEnd = fBufferStart + (long long)(fBuffer.size() / fChannels);
    std::vector<float> sums(fChannels);
    while (true) {
        double t = fOutputFrames * fStep;
        if (final && t >= fInputFrames)
            break;
        long long center = (long long)std::floor(t);
        long long first = center - fHalfWidth + 1;
        long long last = center + fHalfWidth;
        if (last >= bufferEnd)
            break;
        std::fill(sums.begin(), sums.end(), 0.0f);
        for (long long i = std::max(first, fBufferStart); i <= last; i++) {
            float weight = Kernel(t - (double)i);
            const float* frame = &fBuffer[(size_t)(i - fBufferStart) * fChannels];
            for (int c = 0; c < fChannels; c++)
                sums[c] += frame[c] * weight;
        }
        output.insert(output.end(), sums.begin(), sums.end());
        fOutputFrames++;
    }
    // drop the input no future output frame reaches
    long long keepFrom = (long long)std::floor(fOutputFrames * fStep) - fHalfWidth + 1;
    if (keepFrom > fBufferStart) {
        size_t drop = (size_t)std::min<long long>(keepFrom - fBufferStart, (long long)(fBuffer.size() / fChannels));
        fBuffer.erase(fBuffer.begin(), fBuffer.begin() + drop * fChannels);
        fBufferStart += (long long)drop;
    }
}

} // namespace amp
