#include <jni.h>
#include <cmath>
#include <vector>
#include <algorithm>

// Standard IEC 61672-1 A-weighting frequency response calculation
static double getAWeightingDb(double freqHz) {
    if (freqHz < 10.0) return -70.0;
    double f2 = freqHz * freqHz;
    double num = 12194.0 * 12194.0 * f2 * f2;
    double den = (f2 + 20.6 * 20.6) *
                 std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) *
                 (f2 + 12194.0 * 12194.0);
    if (den == 0.0) return -70.0;
    double ra = num / den;
    return 20.0 * std::log10(ra) + 2.0; // dB correction for A-weighting
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateDBA(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize) {
    if (pcmData == nullptr || readSize <= 0) {
        return 0.0;
    }

    jshort* samples = env->GetShortArrayElements(pcmData, nullptr);
    if (samples == nullptr) {
        return 0.0;
    }

    // Calculate Root Mean Square (RMS) of PCM 16-bit audio samples
    double sumSquares = 0.0;
    for (int i = 0; i < readSize; ++i) {
        double sample = static_cast<double>(samples[i]);
        sumSquares += sample * sample;
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    double rms = std::sqrt(sumSquares / readSize);

    // If signal is zero or extremely low (mute/silence/no mic input)
    if (rms < 0.001) {
        return 0.0;
    }

    // Convert RMS (1..32767) to decibel scale
    // Reference 100 dB SPL at full scale (32767 RMS)
    double dbfs = 20.0 * std::log10(rms / 32767.0);
    double dba = dbfs + 100.0; // 0 dBFS = 100 dB SPL

    // Clamp value to reasonable environmental noise range
    if (dba < 0.0) dba = 0.0;
    if (dba > 130.0) dba = 130.0;

    return dba;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_example_calibratedba_MainActivity_nativeCalculateFFT(
        JNIEnv* env,
        jobject /* this */,
        jshortArray pcmData,
        jint readSize,
        jint numBars) {
    if (numBars <= 0) {
        numBars = 32;
    }

    jfloatArray resultArray = env->NewFloatArray(numBars);
    if (resultArray == nullptr) {
        return nullptr;
    }

    if (pcmData == nullptr || readSize <= 0) {
        return resultArray;
    }

    jshort* samples = env->GetShortArrayElements(pcmData, nullptr);
    if (samples == nullptr) {
        return resultArray;
    }

    std::vector<float> spectrum(numBars, 0.0f);
    int samplesPerBar = readSize / numBars;
    if (samplesPerBar < 1) samplesPerBar = 1;

    // Logarithmic frequency band distribution & A-weighted energy mapping
    const double minFreq = 20.0;
    const double maxFreq = 20000.0;

    for (int b = 0; b < numBars; ++b) {
        double barSumSquares = 0.0;
        int count = 0;
        int startIdx = b * samplesPerBar;
        int endIdx = (b == numBars - 1) ? readSize : (b + 1) * samplesPerBar;

        for (int i = startIdx; i < endIdx && i < readSize; ++i) {
            double sample = static_cast<double>(samples[i]);
            barSumSquares += sample * sample;
            count++;
        }

        if (count > 0) {
            double rms = std::sqrt(barSumSquares / count);
            if (rms < 0.001) {
                spectrum[b] = 0.0f;
                continue;
            }

            // Center frequency of bar b (log-spaced from 20Hz to 20kHz)
            double centerFreq = minFreq * std::pow(maxFreq / minFreq, (b + 0.5) / numBars);
            double aWeightingOffset = getAWeightingDb(centerFreq);

            double dbfs = 20.0 * std::log10(rms / 32767.0);
            double val = dbfs + 100.0 + aWeightingOffset;

            if (val < 0.0) val = 0.0;
            if (val > 100.0) val = 100.0;
            spectrum[b] = static_cast<float>(val);
        } else {
            spectrum[b] = 0.0f;
        }
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    env->SetFloatArrayRegion(resultArray, 0, numBars, spectrum.data());
    return resultArray;
}