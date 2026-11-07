#include <jni.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <mutex>
#include <complex>
// ============================================================
// JNI: RETURN [LAeq, LAmax, LAmin]
//
// Calculates dBA sound level metrics from PCM 16-bit audio buffer.
// ============================================================
static double sessionSumSquares = 0.0;
static long long sessionTotalSamples = 0;

// Đảm bảo không có hai luồng cùng thay đổi dữ liệu tích lũy
static std::mutex measurementMutex;

extern "C" JNIEXPORT void JNICALL
Java_com_example_calibratedba_MainActivity_nativeResetMeasurement(
        JNIEnv* env,
        jobject /* this */) {

    std::lock_guard<std::mutex> lock(measurementMutex);

    sessionSumSquares = 0.0;
    sessionTotalSamples = 0;
}

extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateNoiseMetrics(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize,
        jint sampleRate,
        jdouble calibrationOffsetDbA
) {
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

    // Compute RMS energy of PCM audio samples
    double sumSquares = 0.0;

    // 100 ms frame window for Min/Max tracking
    int windowSize = sampleRate / 10; // ~100ms
    if (windowSize <= 0) windowSize = readSize;

    double maxFrameRms = 0.0;
    double minFrameRms = 32767.0;

    for (int start = 0; start < readSize; start += windowSize) {
        int end = std::min(start + windowSize, readSize);
        double frameSum = 0.0;
        int frameCount = 0;

        for (int i = start; i < end; ++i) {
            double s = static_cast<double>(samples[i]);
            frameSum += s * s;
            frameCount++;
        }

        if (frameCount > 0) {
            double frameRms = std::sqrt(frameSum / frameCount);
            if (frameRms > maxFrameRms) maxFrameRms = frameRms;
            if (frameRms < minFrameRms) minFrameRms = frameRms;
        }

        sumSquares += frameSum;
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    // Tích lũy năng lượng của buffer hiện tại
    double overallRms = 0.0;

    {
        std::lock_guard<std::mutex> lock(measurementMutex);

        sessionSumSquares += sumSquares;
        sessionTotalSamples += readSize;

        // Tính RMS từ toàn bộ mẫu trong phiên đo
        if (sessionTotalSamples > 0) {
            overallRms = std::sqrt(
                    sessionSumSquares /
                    static_cast<double>(sessionTotalSamples)
            );
        }
    }

    // Reference SPL Constant: 0 dBFS = 100 dB SPL (Standard mobile microphone mapping)
    const double REF_SPL_DB = 100.0;

    double laeq = 0.0;
    if (overallRms > 0.1) {
        laeq = 20.0 * std::log10(overallRms / 32767.0) + REF_SPL_DB + calibrationOffsetDbA;
    } else {
        laeq = 0.0 + calibrationOffsetDbA;
    }

    double lamax = 0.0;
    if (maxFrameRms > 0.1) {
        lamax = 20.0 * std::log10(maxFrameRms / 32767.0) + REF_SPL_DB + calibrationOffsetDbA;
    } else {
        lamax = laeq;
    }

    double lamin = 0.0;
    if (minFrameRms > 0.1 && minFrameRms <= maxFrameRms) {
        lamin = 20.0 * std::log10(minFrameRms / 32767.0) + REF_SPL_DB + calibrationOffsetDbA;
    } else {
        lamin = laeq;
    }

    if (laeq < 0.0) laeq = 0.0;
    if (lamax < 0.0) lamax = 0.0;
    if (lamin < 0.0) lamin = 0.0;

    const jdouble output[3] = { laeq, lamax, lamin };

    jdoubleArray outputArray = env->NewDoubleArray(3);
    if (outputArray == nullptr) {
        return nullptr;
    }

    env->SetDoubleArrayRegion(outputArray, 0, 3, output);
    return outputArray;
}

// Perform FFT using Cooley-Tukey Radix-2 algorithm
static void performFFT(std::vector<std::complex<double>>& data) {

    const int n = static_cast<int>(data.size());
    const double PI = 3.14;

    // Bit-reversal permutation
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

    // FFT butterfly operations
    for (int len = 2; len <= n; len <<= 1) {

        double angle = -2.0 * PI / len;

        std::complex<double> wLen(
                std::cos(angle),
                std::sin(angle)
        );

        for (int i = 0; i < n; i += len) {

            std::complex<double> w(1.0, 0.0);

            for (int k = 0; k < len / 2; k++) {

                std::complex<double> u = data[i + k];

                std::complex<double> v =
                        data[i + k + len / 2] * w;

                data[i + k] = u + v;

                data[i + k + len / 2] = u - v;

                w *= wLen;
            }
        }
    }
}
// ============================================================
// JNI: CALCULATE FFT SPECTRUM FOR SpectrumView
// ============================================================

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateFFT(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize,
        jint numBars) {

    constexpr int FFT_SIZE = 2048;
    constexpr double SAMPLE_RATE = 44100.0;
    constexpr double MIN_FREQ = 20.0;
    constexpr double MAX_FREQ = 20000.0;

    if (numBars <= 0) {
        return env->NewFloatArray(0);
    }

    std::vector<jfloat> bars(numBars, 0.0f);

    // Validate input
    if (pcmData == nullptr || readSize <= 0) {
        jfloatArray result = env->NewFloatArray(numBars);

        if (result != nullptr) {
            env->SetFloatArrayRegion(
                    result, 0, numBars, bars.data()
            );
        }

        return result;
    }

    jsize arrayLength = env->GetArrayLength(pcmData);

    readSize = std::min(readSize, static_cast<jint>(arrayLength));

    if (readSize <= 0) {
        jfloatArray result = env->NewFloatArray(numBars);

        if (result != nullptr) {
            env->SetFloatArrayRegion(
                    result, 0, numBars, bars.data()
            );
        }

        return result;
    }

    jshort* samples = env->GetShortArrayElements(pcmData, nullptr);

    if (samples == nullptr) {
        return nullptr;
    }

    // Use the latest samples, up to FFT_SIZE
    int validSamples = std::min(readSize, FFT_SIZE);

    int startIndex = readSize - validSamples;

    // Calculate DC offset
    double mean = 0.0;

    for (int i = 0; i < validSamples; i++) {
        mean += samples[startIndex + i];
    }

    mean /= validSamples;

    std::vector<std::complex<double>> fftData(
            FFT_SIZE, std::complex<double>(0.0, 0.0)
    );

    // Apply Hann window
    double windowSum = 0.0;

    for (int i = 0; i < validSamples; i++) {

        double window = 1.0;

        if (validSamples >= 3) {
            window = 0.5 * (
                    1.0 - std::cos(
                            2.0 * 3.14159265358979323846 * i
                            / (validSamples - 1)
                    )
            );
        }

        double normalizedSample =
                (samples[startIndex + i] - mean) / 32768.0;

        fftData[i] = normalizedSample * window;

        windowSum += window;
    }

    env->ReleaseShortArrayElements(
            pcmData, samples, JNI_ABORT
    );

    // Perform actual FFT
    performFFT(fftData);

    if (windowSum <= 0.0) {
        windowSum = 1.0;
    }

    // Divide frequency range into logarithmic bands
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
                static_cast<int>(
                        std::ceil(lowFreq * FFT_SIZE / SAMPLE_RATE)
                )
        );

        int lastBin = std::min(
                FFT_SIZE / 2,
                static_cast<int>(
                        std::floor(highFreq * FFT_SIZE / SAMPLE_RATE)
                )
        );

        double maxMagnitude = 0.0;

        // Find the strongest frequency component in this band
        if (firstBin <= lastBin) {

            for (int bin = firstBin; bin <= lastBin; bin++) {

                double magnitude =
                        2.0 * std::abs(fftData[bin]) / windowSum;

                maxMagnitude = std::max(
                        maxMagnitude, magnitude
                );
            }

        } else {

            // Use the nearest FFT bin if this band is narrower
            // than the FFT frequency resolution.
            double centerFreq = std::sqrt(lowFreq * highFreq);

            int centerBin = static_cast<int>(
                    std::round(centerFreq * FFT_SIZE / SAMPLE_RATE)
            );

            centerBin = std::max(
                    1, std::min(FFT_SIZE / 2, centerBin)
            );

            maxMagnitude =
                    2.0 * std::abs(fftData[centerBin]) / windowSum;
        }

        // Convert magnitude to dBFS
        double dbfs = 20.0 * std::log10(
                std::max(maxMagnitude, 1e-8)
        );

        // Convert -80 dBFS to 0 and 0 dBFS to 100
        double level = (dbfs + 110.0) * 100.0 / 80.0;

        level = std::max(0.0, std::min(100.0, level));

        bars[bar] = static_cast<jfloat>(level);
    }

    // Return 32 frequency bands to Java
    jfloatArray result = env->NewFloatArray(numBars);

    if (result != nullptr) {
        env->SetFloatArrayRegion(
                result,
                0,
                numBars,
                bars.data()
        );
    }

    return result;
}
