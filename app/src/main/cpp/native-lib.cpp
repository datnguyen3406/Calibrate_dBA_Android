#include <jni.h>
#include <cmath>
#include <vector>
#include <algorithm>

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

    double sumSquares = 0.0;
    for (int i = 0; i < readSize; ++i) {
        double sample = static_cast<double>(samples[i]);
        sumSquares += sample * sample;
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    double rms = std::sqrt(sumSquares / readSize);
    if (rms < 1.0) {
        rms = 1.0;
    }

    // Convert RMS (0..32767) to decibel scale
    // Reference 16-bit PCM max RMS is 32767.
    // Full scale dBFS = 20 * log10(rms / 32767.0)
    // Map to ambient SPL range ~ 30 dBA - 120 dBA (Reference offset ~ 96 dB SPL at full scale)
    double dbfs = 20.0 * std::log10(rms / 32767.0);
    double dba = dbfs + 96.0;

    if (dba < 20.0) dba = 20.0;
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
            if (rms < 1.0) rms = 1.0;
            double dbfs = 20.0 * std::log10(rms / 32767.0);
            float val = static_cast<float>(dbfs + 96.0);
            if (val < 0.0f) val = 0.0f;
            if (val > 100.0f) val = 100.0f;
            spectrum[b] = val;
        } else {
            spectrum[b] = 0.0f;
        }
    }

    env->ReleaseShortArrayElements(pcmData, samples, JNI_ABORT);

    env->SetFloatArrayRegion(resultArray, 0, numBars, spectrum.data());
    return resultArray;
}