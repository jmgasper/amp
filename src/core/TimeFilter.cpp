#include "TimeFilter.h"
#include <cmath>

namespace tasamp {

namespace {
const double kAdaptiveForgettingCutoff = 3.0;
const double kMaxErrorScale = 0.5;
const double kDriftSignificanceThresholdSquared = 4.0;
}

TimeFilter::TimeFilter(double processStdDev, double forgetFactor, double driftProcessStdDev)
    : fProcessVariance(processStdDev * processStdDev),
      fDriftProcessVariance(driftProcessStdDev * driftProcessStdDev),
      fForgetVarianceFactor(forgetFactor * forgetFactor)
{
}

void TimeFilter::Reset()
{
    std::lock_guard<std::mutex> lock(fMutex);
    fLastUpdate = 0;
    fCount = 0;
    fOffset = fDrift = 0.0;
    fOffsetCovariance = 1e300;
    fOffsetDriftCovariance = fDriftCovariance = 0.0;
    fCurrent = Element();
}

void TimeFilter::Update(int64_t measurement, int64_t maxError, int64_t timeAdded)
{
    std::lock_guard<std::mutex> lock(fMutex);
    if (timeAdded <= fLastUpdate)
        return;
    double dt = (double)(timeAdded - fLastUpdate);
    fLastUpdate = timeAdded;
    double updateStdDev = (double)maxError * kMaxErrorScale;
    double measurementVariance = updateStdDev * updateStdDev;
    if (fCount <= 0) {
        fCount++;
        fOffset = (double)measurement;
        fOffsetCovariance = measurementVariance;
        fDrift = 0.0;
        fCurrent = {fLastUpdate, fOffset, fDrift, false};
        return;
    }
    if (fCount == 1) {
        fCount++;
        fDrift = ((double)measurement - fOffset) / dt;
        fOffset = (double)measurement;
        fDriftCovariance = (fOffsetCovariance + measurementVariance) / (dt * dt);
        fOffsetCovariance = measurementVariance;
        fCurrent = {fLastUpdate, fOffset, fDrift, false};
        return;
    }
    double offset = fOffset + fDrift * dt;
    double dtSquared = dt * dt;
    double driftProcessVariance = dt * fDriftProcessVariance;
    double newDriftCovariance = fDriftCovariance + driftProcessVariance;
    double newOffsetDriftCovariance = fOffsetDriftCovariance + fDriftCovariance * dt;
    double offsetProcessVariance = dt * fProcessVariance;
    double newOffsetCovariance = fOffsetCovariance + 2 * fOffsetDriftCovariance * dt
        + fDriftCovariance * dtSquared + offsetProcessVariance;
    double residual = (double)measurement - offset;
    double maxResidualCutoff = (double)maxError * kAdaptiveForgettingCutoff;
    if (fCount < 100)
        fCount++;
    else if (std::fabs(residual) > maxResidualCutoff) {
        newDriftCovariance *= fForgetVarianceFactor;
        newOffsetDriftCovariance *= fForgetVarianceFactor;
        newOffsetCovariance *= fForgetVarianceFactor;
    }
    double uncertainty = 1.0 / std::fmax(newOffsetCovariance + measurementVariance, 1e-9);
    double offsetGain = newOffsetCovariance * uncertainty;
    double driftGain = newOffsetDriftCovariance * uncertainty;
    fOffset = offset + offsetGain * residual;
    fDrift += driftGain * residual;
    fDriftCovariance = newDriftCovariance - driftGain * newOffsetDriftCovariance;
    fOffsetDriftCovariance = newOffsetDriftCovariance - driftGain * newOffsetCovariance;
    fOffsetCovariance = newOffsetCovariance - offsetGain * newOffsetCovariance;
    bool useDrift = fDrift * fDrift > kDriftSignificanceThresholdSquared * fDriftCovariance;
    fCurrent = {fLastUpdate, fOffset, fDrift, useDrift};
}

int64_t TimeFilter::ComputeServerTime(int64_t clientTime) const
{
    std::lock_guard<std::mutex> lock(fMutex);
    double offset = fCurrent.offset;
    if (fCurrent.useDrift)
        offset += fCurrent.drift * (double)(clientTime - fCurrent.lastUpdate);
    return clientTime + (int64_t)std::llround(offset);
}

int64_t TimeFilter::ComputeClientTime(int64_t serverTime) const
{
    std::lock_guard<std::mutex> lock(fMutex);
    double offset = fCurrent.offset;
    if (fCurrent.useDrift) {
        // serverTime = clientTime + offset + drift * (clientTime - lastUpdate)
        double denominator = 1.0 + fCurrent.drift;
        double numerator = (double)serverTime - offset + fCurrent.drift * (double)fCurrent.lastUpdate;
        return (int64_t)std::llround(numerator / denominator);
    }
    return serverTime - (int64_t)std::llround(offset);
}

} // namespace tasamp
