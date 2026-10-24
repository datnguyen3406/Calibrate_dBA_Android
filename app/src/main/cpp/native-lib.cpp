#include <jni.h>
#include <cmath>
#include <vector>
#include <algorithm>

// ============================================================
// JNI: RETURN [LAeq, LAmax, LAmin]
//
// Calculates dBA sound level metrics from PCM 16-bit audio buffer.
// ============================================================

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

    double overallRms = std::sqrt(sumSquares / readSize);

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
            if (rms < 0.1) {
                spectrum[b] = 0.0f;
                continue;
            }

            double val = 20.0 * std::log10(rms / 32767.0) + 100.0;
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