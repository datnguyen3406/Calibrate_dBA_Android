#include <jni.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <mutex>
#include <complex>
#include <array>
#include <limits>

// ============================================================================
// 1. GLOBAL CONSTANTS & DSP CONFIGURATION
// ============================================================================

// Reference Sound Pressure Level: 0 dBFS corresponds to 100.0 dB SPL
static constexpr double REF_SPL_DB = 100.0;

// FFT parameters: 4096-point FFT at 44.1 kHz yields ~10.77 Hz frequency resolution
static constexpr int FFT_SIZE = 4096;
static constexpr double FFT_SAMPLE_RATE = 44100.0;
static constexpr double PI = 3.14159265358979323846;


// ============================================================================
// 2. THREAD-SAFE MEASUREMENT STATES & ROLLING BUFFERS
// ============================================================================

// Time-domain accumulators for session LAeq and 100ms frame LAmax / LAmin
static double sessionSumSquares = 0.0;
static long long sessionTotalSamples = 0;
static double windowSumSquares = 0.0;
static int windowSampleCount = 0;
static std::mutex measurementMutex;

// Circular buffer to collect 4096 contiguous samples across multiple AudioRecord reads
static std::array<jshort, FFT_SIZE> fftHistory{};
static int fftWriteIndex = 0;
static int fftHistoryCount = 0;
static std::mutex fftHistoryMutex;


// ============================================================================
// 3. A-WEIGHTING FILTER ENGINE (TIME & FREQUENCY DOMAIN)
// ============================================================================

// State variables for a Direct Form I biquad filter section
struct BiquadState {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;

    void reset() {
        x1 = x2 = 0.0;
        y1 = y2 = 0.0;
    }
};

// IIR A-weighting filter coefficients designed for fs = 44100 Hz.
// Implemented as three cascaded second-order sections (6th-order filter).
// Coefficient format per row: { b0, b1, b2, a1, a2 }
static const double A_WEIGHTING_COEFFICIENTS[3][5] = {
        {
                0.95616638497,
                -1.31960414122,
                0.36343775625,
                -1.31861375911,
                0.32059452332
        },
        {
                0.94317138580,
                -1.88634277160,
                0.94317138580,
                -1.88558607420,
                0.88709946900
        },
        {
                0.69736775447,
                -0.42552769920,
                -0.27184005527,
                -1.31859445445,
                0.32058831623
        }
};

static BiquadState aWeightingStates[3];

// Process a single sample through one biquad section:
// y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
static double processBiquad(
        double input,
        const double* c,
        BiquadState& state) {

    double output =
            c[0] * input
            + c[1] * state.x1
            + c[2] * state.x2
            - c[3] * state.y1
            - c[4] * state.y2;

    // Update delay line states
    state.x2 = state.x1;
    state.x1 = input;

    state.y2 = state.y1;
    state.y1 = output;

    return output;
}

// Cascade a single PCM sample through all three biquad sections
static double applyAWeighting(double input) {
    double output = input;

    for (int i = 0; i < 3; i++) {
        output = processBiquad(
                output,
                A_WEIGHTING_COEFFICIENTS[i],
                aWeightingStates[i]
        );
    }

    return output;
}

// Compute the analytical IEC 61672-1 A-weighting gain (in dB) at a given frequency.
// Used to weight FFT frequency bins in the spectral domain.
static double getAWeightingDb(double frequency) {
    if (frequency <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }

    // Standard analog pole frequencies (Hz) per IEC 61672-1
    constexpr double F1 = 20.6;
    constexpr double F2 = 107.7;
    constexpr double F3 = 737.9;
    constexpr double F4 = 12194.0;

    double f2 = frequency * frequency;

    double numerator = F4 * F4 * f2 * f2;

    double denominator =
            (f2 + F1 * F1)
            * std::sqrt((f2 + F2 * F2) * (f2 + F3 * F3))
            * (f2 + F4 * F4);

    double response = numerator / denominator;

    // +2.0 dB normalization offset ensures 0 dB gain at 1000 Hz
    return 20.0 * std::log10(response) + 2.0;
}


// ============================================================================
// 4. COOLEY-TUKEY RADIX-2 FFT ENGINE
// ============================================================================

// Perform in-place Radix-2 Decimation-In-Time (DIT) Fast Fourier Transform
static void performFFT(std::vector<std::complex<double>>& data) {
    const int n = static_cast<int>(data.size());

    // Step 1: Bit-reversal permutation to reorder input samples in-place
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // Step 2: Butterfly computations across log2(N) stages
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * PI / len;
        std::complex<double> wLen(std::cos(angle), std::sin(angle));

        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; k++) {
                std::complex<double> u = data[i + k];
                std::complex<double> v = data[i + k + len / 2] * w;

                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;

                w *= wLen; // Rotate twiddle factor
            }
        }
    }
}


// ============================================================================
// 5. JNI INTERFACE: RESET MEASUREMENT STATE
// ============================================================================

extern "C" JNIEXPORT void JNICALL
Java_com_example_calibratedba_MainActivity_nativeResetMeasurement(
        JNIEnv* env,
        jobject /* this */) {

    // Clear session energy accumulators, 100ms window states, and IIR filter history
    {
        std::lock_guard<std::mutex> lock(measurementMutex);

        sessionSumSquares = 0.0;
        sessionTotalSamples = 0;
        windowSumSquares = 0.0;
        windowSampleCount = 0;

        for (int i = 0; i < 3; i++) {
            aWeightingStates[i].reset();
        }
    }

    // Clear FFT circular history buffer
    {
        std::lock_guard<std::mutex> lock(fftHistoryMutex);

        fftHistory.fill(0);
        fftWriteIndex = 0;
        fftHistoryCount = 0;
    }
}


// ============================================================================
// 6. JNI INTERFACE: CALCULATE TIME-DOMAIN dBA METRICS [LAeq, LAmax, LAmin]
// ============================================================================

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateNoiseMetrics(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize,
        jint sampleRate,
        jdouble calibrationOffsetDbA) {

    // Validate input parameters and array bounds
    if (pcmData == nullptr || readSize <= 0 || sampleRate <= 0) {
        return nullptr;
    }

    const jsize arrayLength = env->GetArrayLength(pcmData);
    if (readSize > arrayLength) {
        return nullptr;
    }

    jshort* samples = env->GetShortArrayElements(pcmData, nullptr);
    if (samples == nullptr) {
        return nullptr;
    }

    // Standard 100ms integration window for LAmax and LAmin (4410 samples at 44.1 kHz)
    const int windowSize = std::max(1, sampleRate / 10);

    double overallRms = 0.0;
    double maxFrameRms = 0.0;
    double minFrameRms = std::numeric_limits<double>::infinity();
    bool hasValidFrame = false;

    // Filter samples and accumulate energy under mutex lock
    {
        std::lock_guard<std::mutex> lock(measurementMutex);

        for (int i = 0; i < readSize; i++) {
            double input = static_cast<double>(samples[i]);
            double weighted = applyAWeighting(input);
            double squared = weighted * weighted;

            // 1. Accumulate continuous energy for session LAeq
            sessionSumSquares += squared;
            sessionTotalSamples++;

            // 2. Accumulate energy for fixed 100ms window (LAmax / LAmin)
            windowSumSquares += squared;
            windowSampleCount++;

            // Evaluate frame RMS only when a full 100ms window is collected
            if (windowSampleCount >= windowSize) {
                double frameRms = std::sqrt(
                        windowSumSquares / static_cast<double>(windowSampleCount)
                );

                // Ignore exact digital silence and numerical underflow
                if (frameRms > 1e-9) {
                    hasValidFrame = true;
                    maxFrameRms = std::max(maxFrameRms, frameRms);
                    minFrameRms = std::min(minFrameRms, frameRms);
                }

                // Reset 100ms window accumulator for the next frame
                windowSumSquares = 0.0;
                windowSampleCount = 0;
            }
        }

        // Compute overall session RMS for LAeq
        if (sessionTotalSamples > 0) {
            overallRms = std::sqrt(
                    sessionSumSquares /
                    static_cast<double>(sessionTotalSamples)
            );
        }
    }

    // Release native array pointer without copying back to Java heap
    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    const double INVALID_LEVEL = std::numeric_limits<double>::quiet_NaN();

    // Helper lambda: Convert 16-bit A-weighted RMS amplitude to calibrated dBA
    auto rmsToDbA = [&](double rms) -> double {
        if (!std::isfinite(rms) || rms <= 1e-9) {
            return INVALID_LEVEL;
        }
        return 20.0 * std::log10(rms / 32767.0)
               + REF_SPL_DB
               + calibrationOffsetDbA;
    };

    double laeq = rmsToDbA(overallRms);
    double lamax = INVALID_LEVEL;
    double lamin = INVALID_LEVEL;

    // Return valid Min/Max only if at least one full 100ms window completed in this read
    if (hasValidFrame) {
        lamax = rmsToDbA(maxFrameRms);
        lamin = rmsToDbA(minFrameRms);
    }

    const jdouble output[3] = { laeq, lamax, lamin };

    jdoubleArray outputArray = env->NewDoubleArray(3);
    if (outputArray == nullptr) {
        return nullptr;
    }

    env->SetDoubleArrayRegion(outputArray, 0, 3, output);
    return outputArray;
}


// ============================================================================
// 7. JNI INTERFACE: CALCULATE A-WEIGHTED FFT SPECTRUM (0 - 100 dBA)
// ============================================================================

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateFFT(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize,
        jint numBars,
        jdouble calibrationOffsetDbA) {

    constexpr double MIN_FREQ = 20.0;
    constexpr double MAX_FREQ = 20000.0;

    if (numBars <= 0) {
        return env->NewFloatArray(0);
    }

    std::vector<jfloat> bars(numBars, 0.0f);
    jfloatArray result = env->NewFloatArray(numBars);
    if (result == nullptr) {
        return nullptr;
    }

    // Return zeroed spectrum on invalid input
    if (pcmData == nullptr || readSize <= 0) {
        env->SetFloatArrayRegion(result, 0, numBars, bars.data());
        return result;
    }

    jsize arrayLength = env->GetArrayLength(pcmData);
    readSize = std::min(readSize, static_cast<jint>(arrayLength));

    if (readSize <= 0) {
        env->SetFloatArrayRegion(result, 0, numBars, bars.data());
        return result;
    }

    jshort* samples = env->GetShortArrayElements(pcmData, nullptr);
    if (samples == nullptr) {
        return result;
    }

    // ------------------------------------------------------------------------
    // STEP 1: APPEND SAMPLES TO 4096-SAMPLE CIRCULAR ROLLING BUFFER
    // ------------------------------------------------------------------------
    std::vector<jshort> fftFrame;
    fftFrame.reserve(FFT_SIZE);

    {
        std::lock_guard<std::mutex> lock(fftHistoryMutex);

        for (int i = 0; i < readSize; i++) {
            fftHistory[fftWriteIndex] = samples[i];
            fftWriteIndex = (fftWriteIndex + 1) % FFT_SIZE;

            if (fftHistoryCount < FFT_SIZE) {
                fftHistoryCount++;
            }
        }

        // Extract chronological 4096-sample frame once buffer is full
        if (fftHistoryCount == FFT_SIZE) {
            fftFrame.resize(FFT_SIZE);
            for (int i = 0; i < FFT_SIZE; i++) {
                int index = (fftWriteIndex + i) % FFT_SIZE;
                fftFrame[i] = fftHistory[index];
            }
        }
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    // Wait until a full 4096-sample window has been accumulated
    if (fftFrame.size() != FFT_SIZE) {
        env->SetFloatArrayRegion(result, 0, numBars, bars.data());
        return result;
    }

    // ------------------------------------------------------------------------
    // STEP 2: REMOVE DC OFFSET & APPLY HANN WINDOW
    // ------------------------------------------------------------------------
    double mean = 0.0;
    for (int i = 0; i < FFT_SIZE; i++) {
        mean += fftFrame[i];
    }
    mean /= FFT_SIZE;

    std::vector<std::complex<double>> fftData(
            FFT_SIZE,
            std::complex<double>(0.0, 0.0)
    );

    double windowSum = 0.0;
    for (int i = 0; i < FFT_SIZE; i++) {
        // Hann window reduces spectral leakage at frame boundaries
        double window = 0.5 * (
                1.0 - std::cos(2.0 * PI * i / (FFT_SIZE - 1))
        );

        // Normalize 16-bit PCM to [-1.0, 1.0] after subtracting DC mean
        double normalizedSample =
                (static_cast<double>(fftFrame[i]) - mean) / 32768.0;

        fftData[i] = normalizedSample * window;
        windowSum += window;
    }

    // ------------------------------------------------------------------------
    // STEP 3: EXECUTE COOLEY-TUKEY RADIX-2 FFT
    // ------------------------------------------------------------------------
    performFFT(fftData);

    if (windowSum <= 0.0) {
        env->SetFloatArrayRegion(result, 0, numBars, bars.data());
        return result;
    }

    const double binFrequency = FFT_SAMPLE_RATE / FFT_SIZE;

    // ------------------------------------------------------------------------
    // STEP 4: MAP FFT BINS TO LOGARITHMIC FREQUENCY BANDS (20 Hz - 20 kHz)
    // ------------------------------------------------------------------------
    for (int bar = 0; bar < numBars; bar++) {

        double lowFreq = MIN_FREQ * std::pow(
                MAX_FREQ / MIN_FREQ,
                static_cast<double>(bar) / numBars
        );

        double highFreq = MIN_FREQ * std::pow(
                MAX_FREQ / MIN_FREQ,
                static_cast<double>(bar + 1) / numBars
        );

        int firstBin = std::max(
                1,
                static_cast<int>(std::ceil(lowFreq / binFrequency))
        );

        int endBin = std::min(
                FFT_SIZE / 2,
                static_cast<int>(std::ceil(highFreq / binFrequency))
        );

        double powerSum = 0.0;
        double maxBinPower = 0.0;
        int binCount = 0;

        // --------------------------------------------------------------------
        // STEP 5: COMPUTE A-WEIGHTED RMS POWER PER BAND
        // --------------------------------------------------------------------
        if (firstBin < endBin) {

            for (int bin = firstBin; bin < endBin; bin++) {
                double freq = bin * binFrequency;
                double aWeightDb = getAWeightingDb(freq);
                double aWeightLinearPower = std::pow(10.0, aWeightDb / 10.0);

                // Scale one-sided FFT peak amplitude by sqrt(2) (1.41421356) to obtain RMS
                double rmsMagnitude = 1.41421356 * std::abs(fftData[bin]) / windowSum;
                double power = (rmsMagnitude * rmsMagnitude) * aWeightLinearPower;

                powerSum += power;
                maxBinPower = std::max(maxBinPower, power);
                binCount++;
            }

        } else {
            // Fallback for narrow low-frequency bands smaller than 1 FFT bin (~10.77 Hz)
            double centerFreq = std::sqrt(lowFreq * highFreq);

            int centerBin = static_cast<int>(
                    std::round(centerFreq / binFrequency)
            );

            centerBin = std::max(
                    1,
                    std::min(FFT_SIZE / 2 - 1, centerBin)
            );

            double aWeightDb = getAWeightingDb(centerFreq);
            double aWeightLinearPower = std::pow(10.0, aWeightDb / 10.0);

            double rmsMagnitude = 1.41421356 * std::abs(fftData[centerBin]) / windowSum;
            double power = (rmsMagnitude * rmsMagnitude) * aWeightLinearPower;

            powerSum = power;
            maxBinPower = power;
            binCount = 1;
        }

        if (binCount <= 0) {
            bars[bar] = 0.0f;
            continue;
        }

        // --------------------------------------------------------------------
        // STEP 6: CONVERT BAND POWER TO CALIBRATED dBA (0 - 100 dB SCALE)
        // --------------------------------------------------------------------

        // Normalize by binCount to prevent high-frequency bands (with 100-360 bins)
        // from artificially inflating by +20 to +25 dB
        double avgPower = powerSum / static_cast<double>(binCount);

        // Blend average band power with local peak power to preserve narrowband voice harmonics
        double representativePower = std::max(avgPower, maxBinPower * 0.5);

        // +12.0 dB compensation accounts for broadband energy distributed across narrow FFT bins
        constexpr double SPECTRAL_DENSITY_COMPENSATION_DB = 12.0;

        double dbfs = 10.0 * std::log10(std::max(representativePower, 1e-22));

        double bandDbA = dbfs
                         + REF_SPL_DB
                         + SPECTRAL_DENSITY_COMPENSATION_DB
                         + calibrationOffsetDbA;

        // Clamp final bar height to [0.0, 100.0] dB display range
        double level = std::max(0.0, std::min(100.0, bandDbA));

        bars[bar] = static_cast<jfloat>(level);
    }

    env->SetFloatArrayRegion(result, 0, numBars, bars.data());
    return result;
}