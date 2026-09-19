// Two-dimensional Kalman filter mapping the Sendspin server clock to the local clock.
// Port of the reference implementation used by aiosendspin / ESPHome.
#pragma once
#include <cstdint>
#include <mutex>

namespace tasamp {

class TimeFilter {
public:
    TimeFilter(double processStdDev = 0.0, double forgetFactor = 2.0, double driftProcessStdDev = 1e-11);

    // measurement = ((T2-T1)+(T3-T4))/2 ; maxError = ((T4-T1)-(T3-T2))/2 ; timeAdded = client time (T4)
    void Update(int64_t measurement, int64_t maxError, int64_t timeAdded);
    int64_t ComputeServerTime(int64_t clientTime) const;
    int64_t ComputeClientTime(int64_t serverTime) const;
    int Count() const { return fCount; }
    bool Converged() const { return fCount >= 3; }
    double Offset() const { return fOffset; }
    void Reset();

private:
    struct Element {
        int64_t lastUpdate = 0;
        double offset = 0.0;
        double drift = 0.0;
        bool useDrift = false;
    };
    int64_t fLastUpdate = 0;
    int fCount = 0;
    double fOffset = 0.0;
    double fDrift = 0.0;
    double fOffsetCovariance = 1e300;
    double fOffsetDriftCovariance = 0.0;
    double fDriftCovariance = 0.0;
    double fProcessVariance;
    double fDriftProcessVariance;
    double fForgetVarianceFactor;
    Element fCurrent;
    mutable std::mutex fMutex;
};

} // namespace tasamp
