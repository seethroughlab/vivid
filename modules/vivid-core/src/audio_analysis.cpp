// Vivid - Audio Analysis Implementation

#define _USE_MATH_DEFINES
#include <vivid/audio_analysis.h>
#include <vivid/dsp_utils.h>
#include <cmath>
#include <algorithm>
#include <complex>
#include <limits>
#include <sstream>
#include <vector>
#include <numeric>

namespace vivid {

// Frequency band edges (Hz) matching BandSplit operator
static constexpr float BAND_EDGES[] = {0.0f, 60.0f, 250.0f, 500.0f, 2000.0f, 4000.0f, 20000.0f};
static constexpr int NUM_BANDS = 6;

// Compute energy in a frequency range using Goertzel algorithm
// More efficient than full FFT for a small number of frequency bins
static float bandEnergy(const float* monoSamples, uint32_t frameCount,
                        uint32_t sampleRate, float lowHz, float highHz) {
    if (frameCount == 0 || lowHz >= highHz) return 0.0f;

    // Number of frequency bins to sample within this band
    // Use enough bins for reasonable coverage but keep it cheap
    float bandWidth = highHz - lowHz;
    int numBins = std::max(2, std::min(16, static_cast<int>(bandWidth / 20.0f)));

    double totalEnergy = 0.0;

    for (int b = 0; b < numBins; b++) {
        float freq = lowHz + (bandWidth * (b + 0.5f)) / numBins;
        if (freq <= 0.0f || freq >= sampleRate / 2.0f) continue;

        // Goertzel algorithm for single frequency bin
        double k = 0.5 + ((double)frameCount * freq / sampleRate);
        double w = (2.0 * M_PI * k) / frameCount;
        double cosw = std::cos(w);
        double coeff = 2.0 * cosw;

        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (uint32_t i = 0; i < frameCount; i++) {
            s0 = monoSamples[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        totalEnergy += power;
    }

    // Normalize by frame count and number of bins
    return static_cast<float>(std::sqrt(totalEnergy / (numBins * (double)frameCount * frameCount)));
}

AudioAnalysis analyzeAudioBuffer(const float* samples, uint32_t frameCount,
                                  uint32_t channels, uint32_t sampleRate) {
    AudioAnalysis result;

    if (!samples || frameCount == 0 || channels == 0 || sampleRate == 0) {
        return result;
    }

    result.duration = static_cast<float>(frameCount) / sampleRate;

    uint32_t totalSamples = frameCount * channels;

    // Compute RMS, peak, DC offset, clipping count (all in single pass)
    double sumSq = 0.0;
    double sumSqLeft = 0.0;
    double sumSqRight = 0.0;
    double sumDC = 0.0;
    int clippedCount = 0;
    float peak = 0.0f;

    for (uint32_t i = 0; i < totalSamples; i++) {
        float s = samples[i];
        float absS = std::fabs(s);
        sumSq += (double)s * s;
        sumDC += (double)s;
        peak = std::max(peak, absS);
        if (absS >= 0.99f) clippedCount++;

        if (channels >= 2) {
            uint32_t ch = i % channels;
            if (ch == 0) sumSqLeft += (double)s * s;
            else if (ch == 1) sumSqRight += (double)s * s;
        }
    }

    result.rmsLevel = static_cast<float>(std::sqrt(sumSq / totalSamples));
    result.peakLevel = peak;
    result.isSilent = result.rmsLevel < 0.001f;
    result.crestFactor = result.rmsLevel > 0.0001f ? peak / result.rmsLevel : 0.0f;
    result.dcOffset = static_cast<float>(sumDC / totalSamples);
    result.clippedSampleCount = clippedCount;
    result.clippedSamplePct = static_cast<float>(clippedCount) / static_cast<float>(totalSamples);

    if (channels >= 2) {
        result.rmsLeft = static_cast<float>(std::sqrt(sumSqLeft / frameCount));
        result.rmsRight = static_cast<float>(std::sqrt(sumSqRight / frameCount));

        // Stereo correlation (Pearson) and width (side/mid RMS ratio)
        double meanL = 0.0, meanR = 0.0;
        for (uint32_t i = 0; i < frameCount; i++) {
            meanL += samples[i * channels];
            meanR += samples[i * channels + 1];
        }
        meanL /= frameCount;
        meanR /= frameCount;

        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
        double sumMidSq = 0.0, sumSideSq = 0.0;
        for (uint32_t i = 0; i < frameCount; i++) {
            double l = samples[i * channels] - meanL;
            double r = samples[i * channels + 1] - meanR;
            sumLR += l * r;
            sumLL += l * l;
            sumRR += r * r;

            double rawL = samples[i * channels];
            double rawR = samples[i * channels + 1];
            double mid = (rawL + rawR) * 0.5;
            double side = (rawL - rawR) * 0.5;
            sumMidSq += mid * mid;
            sumSideSq += side * side;
        }

        double denom = std::sqrt(sumLL * sumRR);
        result.stereoCorrelation = denom > 0 ? static_cast<float>(sumLR / denom) : 1.0f;

        double midRms = std::sqrt(sumMidSq / frameCount);
        double sideRms = std::sqrt(sumSideSq / frameCount);
        result.stereoWidth = midRms > 0.0001 ? static_cast<float>(sideRms / midRms) : 0.0f;
    } else {
        result.rmsLeft = result.rmsLevel;
        result.rmsRight = result.rmsLevel;
        result.stereoCorrelation = 1.0f;
        result.stereoWidth = 0.0f;
    }

    // Mix down to mono for spectrum analysis
    std::vector<float> mono(frameCount);
    if (channels == 1) {
        std::copy(samples, samples + frameCount, mono.begin());
    } else {
        for (uint32_t i = 0; i < frameCount; i++) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < channels; ch++) {
                sum += samples[i * channels + ch];
            }
            mono[i] = sum / channels;
        }
    }

    // Zero crossing rate on mono signal
    {
        int crossings = 0;
        for (uint32_t i = 1; i < frameCount; i++) {
            if ((mono[i] >= 0.0f) != (mono[i - 1] >= 0.0f)) {
                crossings++;
            }
        }
        result.zeroCrossingRate = result.duration > 0.0f
            ? static_cast<float>(crossings) / result.duration
            : 0.0f;
    }

    // Compute 6-band spectrum using Goertzel
    for (int b = 0; b < NUM_BANDS; b++) {
        result.spectrum[b] = bandEnergy(mono.data(), frameCount,
                                         sampleRate, BAND_EDGES[b], BAND_EDGES[b + 1]);
    }

    // STFT-based spectral metrics (requires >= 2048 frames)
    if (frameCount >= 2048) {
        const int fftSize = 2048;
        const int hop = 512;
        const int numBins = fftSize / 2;
        const float binHz = static_cast<float>(sampleRate) / fftSize;

        auto window = dsp::hannWindow(fftSize);

        int numFrames = (static_cast<int>(frameCount) - fftSize) / hop + 1;
        if (numFrames > 0) {
            std::vector<std::complex<float>> fftBuf(fftSize);
            std::vector<float> prevMag(numBins, 0.0f);
            std::vector<float> fluxValues;  // stored for onset detection

            double sumCentroid = 0.0, sumSpread = 0.0;
            double sumFlatness = 0.0, sumRolloff = 0.0;
            double sumFlux = 0.0;
            float maxFlux = 0.0f;

            for (int f = 0; f < numFrames; f++) {
                int offset = f * hop;

                // Apply window and load
                for (int i = 0; i < fftSize; i++) {
                    fftBuf[i] = std::complex<float>(mono[offset + i] * window[i], 0.0f);
                }
                dsp::fft(fftBuf.data(), fftSize);

                // Compute power spectrum
                std::vector<float> mag(numBins);
                double totalPower = 0.0;
                for (int b = 0; b < numBins; b++) {
                    mag[b] = std::abs(fftBuf[b]);
                    totalPower += (double)mag[b] * mag[b];
                }

                if (totalPower > 1e-12) {
                    // Spectral centroid: weighted mean frequency
                    double weightedSum = 0.0;
                    double magSum = 0.0;
                    for (int b = 0; b < numBins; b++) {
                        double freq = (b + 0.5) * binHz;
                        weightedSum += freq * mag[b];
                        magSum += mag[b];
                    }
                    double centroid = magSum > 0 ? weightedSum / magSum : 0.0;
                    sumCentroid += centroid;

                    // Spectral spread: weighted std dev
                    double spreadSum = 0.0;
                    for (int b = 0; b < numBins; b++) {
                        double freq = (b + 0.5) * binHz;
                        double diff = freq - centroid;
                        spreadSum += diff * diff * mag[b];
                    }
                    sumSpread += std::sqrt(magSum > 0 ? spreadSum / magSum : 0.0);

                    // Spectral flatness: geometric/arithmetic mean of power
                    double logSum = 0.0;
                    double arithSum = 0.0;
                    int validBins = 0;
                    for (int b = 0; b < numBins; b++) {
                        double power = (double)mag[b] * mag[b];
                        if (power > 1e-20) {
                            logSum += std::log(power);
                            validBins++;
                        }
                        arithSum += power;
                    }
                    if (validBins > 0 && arithSum > 1e-20) {
                        double geoMean = std::exp(logSum / validBins);
                        double arithMean = arithSum / numBins;
                        sumFlatness += std::min(1.0, geoMean / arithMean);
                    }

                    // Spectral rolloff: 85th percentile cumulative energy
                    double cumEnergy = 0.0;
                    double threshold85 = totalPower * 0.85;
                    double rolloff = 0.0;
                    for (int b = 0; b < numBins; b++) {
                        cumEnergy += (double)mag[b] * mag[b];
                        if (cumEnergy >= threshold85) {
                            rolloff = (b + 0.5) * binHz;
                            break;
                        }
                    }
                    sumRolloff += rolloff;
                }

                // Spectral flux: half-wave rectified difference from previous frame
                if (f > 0) {
                    float flux = 0.0f;
                    for (int b = 0; b < numBins; b++) {
                        float diff = mag[b] - prevMag[b];
                        if (diff > 0) flux += diff;
                    }
                    flux /= numBins;  // normalize
                    sumFlux += flux;
                    maxFlux = std::max(maxFlux, flux);
                    fluxValues.push_back(flux);
                }

                prevMag = mag;
            }

            int fluxFrames = numFrames > 1 ? numFrames - 1 : 1;
            result.spectralCentroid = static_cast<float>(sumCentroid / numFrames);
            result.spectralSpread = static_cast<float>(sumSpread / numFrames);
            result.spectralFlux = static_cast<float>(sumFlux / fluxFrames);
            result.spectralFluxMax = maxFlux;
            result.spectralFlatness = static_cast<float>(sumFlatness / numFrames);
            result.spectralRolloff = static_cast<float>(sumRolloff / numFrames);

            // Onset detection from flux values
            // Requires >= 0.5s duration and >= 3 flux frames
            if (result.duration >= 0.5f && fluxValues.size() >= 3) {
                // Adaptive threshold: median(flux) + 0.5 * std(flux)
                std::vector<float> sorted = fluxValues;
                std::sort(sorted.begin(), sorted.end());
                float median = sorted[sorted.size() / 2];

                double fluxMean = 0.0;
                for (float v : fluxValues) fluxMean += v;
                fluxMean /= fluxValues.size();

                double fluxVar = 0.0;
                for (float v : fluxValues) {
                    double d = v - fluxMean;
                    fluxVar += d * d;
                }
                float fluxStd = static_cast<float>(std::sqrt(fluxVar / fluxValues.size()));
                float onsetThreshold = median + 0.5f * fluxStd;
                // Minimum absolute threshold to avoid false positives on steady signals
                onsetThreshold = std::max(onsetThreshold, 0.001f);

                // Peak-pick with minimum inter-onset interval of 50ms
                int minInterOnset = static_cast<int>(0.05f * sampleRate / hop);
                if (minInterOnset < 1) minInterOnset = 1;

                int onsets = 0;
                int lastOnset = -minInterOnset;
                for (size_t i = 1; i + 1 < fluxValues.size(); i++) {
                    if (fluxValues[i] > onsetThreshold &&
                        fluxValues[i] > fluxValues[i - 1] &&
                        fluxValues[i] >= fluxValues[i + 1] &&
                        static_cast<int>(i) - lastOnset >= minInterOnset) {
                        onsets++;
                        lastOnset = static_cast<int>(i);
                    }
                }

                result.onsetCount = onsets;
                result.onsetDensity = result.duration > 0.0f
                    ? static_cast<float>(onsets) / result.duration
                    : 0.0f;
            }
        }
    }

    // LUFS loudness (EBU R128 / ITU-R BS.1770-4)
    // Only for 48kHz and 44100Hz (precomputed K-weighting coefficients)
    if (frameCount >= sampleRate / 10 && (sampleRate == 48000 || sampleRate == 44100)) {
        // Biquad filter (Direct Form II Transposed)
        struct Biquad {
            double b0, b1, b2, a1, a2;
            double z1 = 0.0, z2 = 0.0;

            double process(double x) {
                double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void reset() { z1 = z2 = 0.0; }
        };

        // K-weighting: high shelf + high-pass filter
        // Coefficients from ITU-R BS.1770-4
        Biquad highShelf, highPass;

        if (sampleRate == 48000) {
            // High shelf (stage 1)
            highShelf.b0 = 1.53512485958697;
            highShelf.b1 = -2.69169618940638;
            highShelf.b2 = 1.19839281085285;
            highShelf.a1 = -1.69065929318241;
            highShelf.a2 = 0.73248077421585;
            // High pass (stage 2)
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = -1.99004745483398;
            highPass.a2 = 0.99007225036621;
        } else { // 44100
            highShelf.b0 = 1.5308412300498355;
            highShelf.b1 = -2.6509799951547297;
            highShelf.b2 = 1.1690790799215869;
            highShelf.a1 = -1.6636551132560204;
            highShelf.a2 = 0.7125954280732254;
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = -1.9891696736297957;
            highPass.a2 = 0.9891990357870394;
        }

        // K-weight the signal (process each channel separately, then compute mean square)
        // For mono, treat as single channel with weight 1.0
        uint32_t numCh = std::min(channels, 2u);

        // K-weighted mean square per 400ms block (with 75% overlap)
        uint32_t blockSize400ms = sampleRate * 4 / 10;  // 400ms
        uint32_t hopBlock = blockSize400ms / 4;           // 75% overlap → 100ms hop

        // First, K-weight the entire signal per channel
        std::vector<std::vector<double>> kWeighted(numCh, std::vector<double>(frameCount));
        for (uint32_t ch = 0; ch < numCh; ch++) {
            Biquad hs = highShelf;
            Biquad hp = highPass;
            for (uint32_t i = 0; i < frameCount; i++) {
                double s = channels == 1 ? samples[i] : samples[i * channels + ch];
                double y = hs.process(s);
                kWeighted[ch][i] = hp.process(y);
            }
        }

        // Compute mean square per 400ms block
        std::vector<double> blockLoudness;
        for (uint32_t start = 0; start + blockSize400ms <= frameCount; start += hopBlock) {
            double sumSqBlock = 0.0;
            for (uint32_t ch = 0; ch < numCh; ch++) {
                double chSum = 0.0;
                for (uint32_t i = start; i < start + blockSize400ms; i++) {
                    chSum += kWeighted[ch][i] * kWeighted[ch][i];
                }
                sumSqBlock += chSum / blockSize400ms;
            }
            blockLoudness.push_back(sumSqBlock);
        }

        if (!blockLoudness.empty()) {
            // Momentary: last 400ms block
            double momentaryPower = blockLoudness.back();
            if (momentaryPower > 1e-20) {
                result.momentaryLUFS = static_cast<float>(-0.691 + 10.0 * std::log10(momentaryPower));
            }

            // Short-term: 3s window (last 30 blocks at 100ms hop)
            uint32_t shortTermBlocks = std::min(static_cast<uint32_t>(blockLoudness.size()), sampleRate * 3 / hopBlock);
            if (shortTermBlocks > 0) {
                double stSum = 0.0;
                for (uint32_t i = static_cast<uint32_t>(blockLoudness.size()) - shortTermBlocks;
                     i < blockLoudness.size(); i++) {
                    stSum += blockLoudness[i];
                }
                double stPower = stSum / shortTermBlocks;
                if (stPower > 1e-20) {
                    result.shortTermLUFS = static_cast<float>(-0.691 + 10.0 * std::log10(stPower));
                }
            }

            // Integrated: gated measurement
            // Absolute gate: -70 LUFS
            double absGateThreshold = std::pow(10.0, (-70.0 + 0.691) / 10.0);

            // Compute block LUFS for gating
            std::vector<double> gatedBlocks;
            for (double bl : blockLoudness) {
                if (bl > absGateThreshold) {
                    gatedBlocks.push_back(bl);
                }
            }

            if (!gatedBlocks.empty()) {
                // Relative gate: mean of abs-gated - 10 dB
                double absGatedMean = 0.0;
                for (double bl : gatedBlocks) absGatedMean += bl;
                absGatedMean /= gatedBlocks.size();

                double relGateThreshold = absGatedMean * std::pow(10.0, -10.0 / 10.0);

                double integratedSum = 0.0;
                int integratedCount = 0;
                for (double bl : gatedBlocks) {
                    if (bl > relGateThreshold) {
                        integratedSum += bl;
                        integratedCount++;
                    }
                }

                if (integratedCount > 0) {
                    double integratedPower = integratedSum / integratedCount;
                    result.integratedLUFS = static_cast<float>(-0.691 + 10.0 * std::log10(integratedPower));
                }
            }

            // Loudness Range (LRA): 10th-95th percentile of short-term blocks
            // Short-term blocks: 3s window, 1s hop
            uint32_t stBlockSize = sampleRate * 3;
            uint32_t stHop = sampleRate;
            std::vector<double> stLoudnessValues;
            for (uint32_t start = 0; start + stBlockSize <= frameCount; start += stHop) {
                double stPower = 0.0;
                for (uint32_t ch = 0; ch < numCh; ch++) {
                    double chSum = 0.0;
                    for (uint32_t i = start; i < start + stBlockSize; i++) {
                        chSum += kWeighted[ch][i] * kWeighted[ch][i];
                    }
                    stPower += chSum / stBlockSize;
                }
                if (stPower > absGateThreshold) {
                    stLoudnessValues.push_back(-0.691 + 10.0 * std::log10(stPower));
                }
            }

            if (stLoudnessValues.size() >= 2) {
                std::sort(stLoudnessValues.begin(), stLoudnessValues.end());
                size_t idx10 = static_cast<size_t>(stLoudnessValues.size() * 0.10);
                size_t idx95 = std::min(static_cast<size_t>(stLoudnessValues.size() * 0.95),
                                        stLoudnessValues.size() - 1);
                result.loudnessRange = static_cast<float>(stLoudnessValues[idx95] - stLoudnessValues[idx10]);
            }
        }

        // True peak: 4x linear interpolation
        float tp = 0.0f;
        for (uint32_t ch = 0; ch < numCh; ch++) {
            for (uint32_t i = 0; i + 3 < frameCount; i++) {
                // 4-point Lagrange interpolation at 4x oversampling
                float s0 = channels == 1 ? samples[i] : samples[i * channels + ch];
                float s1 = channels == 1 ? samples[i + 1] : samples[(i + 1) * channels + ch];
                float s2 = channels == 1 ? samples[i + 2] : samples[(i + 2) * channels + ch];
                float s3 = channels == 1 ? samples[i + 3] : samples[(i + 3) * channels + ch];

                // Check inter-sample peaks at 0.25, 0.5, 0.75 between s1 and s2
                for (float t : {0.25f, 0.5f, 0.75f}) {
                    float tm1 = t - 1.0f;
                    float tp1 = t + 1.0f;
                    float tp2 = t + 2.0f;
                    // Cubic Lagrange on s0,s1,s2,s3 centered between s1-s2
                    // t=0→s1, t=1→s2
                    float val = s0 * (-t * tm1 * (t - 2.0f)) / 6.0f
                              + s1 * (tp1 * tm1 * (t - 2.0f)) / 2.0f
                              + s2 * (-tp1 * t * (t - 2.0f)) / 2.0f
                              + s3 * (tp1 * t * tm1) / 6.0f;
                    tp = std::max(tp, std::fabs(val));
                }

                // Also check original samples
                tp = std::max(tp, std::fabs(s1));
            }
        }
        result.truePeak = tp;
        result.truePeakDBTP = tp > 1e-10f
            ? static_cast<float>(20.0 * std::log10(static_cast<double>(tp)))
            : -std::numeric_limits<float>::infinity();
    }

    // YIN pitch detection on mono signal
    if (frameCount >= 1024) {
        uint32_t W = std::min(frameCount, 2048u);
        // tau range: 47Hz to 5kHz
        uint32_t tauMin = sampleRate / 5000;
        uint32_t tauMax = std::min(sampleRate / 47, W / 2);
        if (tauMin < 1) tauMin = 1;

        if (tauMax > tauMin + 1) {
            // Step 1-2: Difference function + cumulative mean normalized difference
            std::vector<double> d(tauMax + 1, 0.0);
            for (uint32_t tau = 1; tau <= tauMax; tau++) {
                double sum = 0.0;
                for (uint32_t j = 0; j < W - tau; j++) {
                    double diff = (double)mono[j] - (double)mono[j + tau];
                    sum += diff * diff;
                }
                d[tau] = sum;
            }

            // Cumulative mean normalized difference
            std::vector<double> dPrime(tauMax + 1, 1.0);
            dPrime[0] = 1.0;
            double runningSum = 0.0;
            for (uint32_t tau = 1; tau <= tauMax; tau++) {
                runningSum += d[tau];
                dPrime[tau] = runningSum > 0 ? d[tau] * tau / runningSum : 1.0;
            }

            // Step 3: Absolute threshold at 0.1
            float yinThreshold = 0.1f;
            uint32_t bestTau = 0;
            double bestVal = 1.0;

            for (uint32_t tau = tauMin; tau <= tauMax; tau++) {
                if (dPrime[tau] < yinThreshold) {
                    // Find local minimum
                    if (tau + 1 <= tauMax && dPrime[tau + 1] < dPrime[tau]) continue;
                    bestTau = tau;
                    bestVal = dPrime[tau];
                    break;
                }
            }

            // If no value below threshold, use global minimum
            if (bestTau == 0) {
                for (uint32_t tau = tauMin; tau <= tauMax; tau++) {
                    if (dPrime[tau] < bestVal) {
                        bestVal = dPrime[tau];
                        bestTau = tau;
                    }
                }
            }

            if (bestTau > 0 && bestVal < 0.5) {
                // Step 4: Parabolic interpolation
                double tau0 = bestTau > 1 ? dPrime[bestTau - 1] : dPrime[bestTau];
                double tau1 = dPrime[bestTau];
                double tau2 = bestTau + 1 <= tauMax ? dPrime[bestTau + 1] : dPrime[bestTau];

                double shift = 0.0;
                double denom2 = 2.0 * (2.0 * tau1 - tau0 - tau2);
                if (std::fabs(denom2) > 1e-10) {
                    shift = (tau0 - tau2) / denom2;
                }
                double refinedTau = bestTau + shift;

                result.pitchHz = static_cast<float>(sampleRate / refinedTau);
                result.pitchConfidence = static_cast<float>(1.0 - bestVal);

                // Hz to MIDI note: 12 * log2(hz/440) + 69
                if (result.pitchHz > 20.0f && result.pitchHz < 10000.0f) {
                    double midiNote = 12.0 * std::log2(result.pitchHz / 440.0) + 69.0;
                    int roundedNote = static_cast<int>(std::round(midiNote));
                    result.pitchCents = static_cast<float>((midiNote - roundedNote) * 100.0);

                    static const char* noteNames[] = {
                        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
                    };
                    int noteIndex = ((roundedNote % 12) + 12) % 12;
                    int octave = (roundedNote / 12) - 1;
                    result.pitchNote = std::string(noteNames[noteIndex]) + std::to_string(octave);
                }

                // HNR: autocorrelation at fundamental period
                if (result.pitchConfidence > 0.3f && bestTau < frameCount) {
                    double r0 = 0.0, rTau = 0.0;
                    uint32_t N = std::min(frameCount - bestTau, W);
                    for (uint32_t j = 0; j < N; j++) {
                        r0 += (double)mono[j] * mono[j];
                        rTau += (double)mono[j] * mono[j + bestTau];
                    }
                    if (r0 > 1e-12 && rTau > 0) {
                        double acRatio = rTau / r0;
                        if (acRatio > 0 && acRatio < 1.0) {
                            result.harmonicToNoiseRatio = static_cast<float>(
                                10.0 * std::log10(acRatio / (1.0 - acRatio)));
                        } else if (acRatio >= 1.0) {
                            result.harmonicToNoiseRatio = 40.0f;  // cap
                        }
                    }
                }
            }
        }
    }

    // Dynamic range: 100ms block RMS analysis
    if (result.duration >= 0.2f) {
        uint32_t blockFrames = sampleRate / 10;  // 100ms blocks
        uint32_t numBlocks = frameCount / blockFrames;
        if (numBlocks >= 2) {
            std::vector<double> blockRms;
            for (uint32_t b = 0; b < numBlocks; b++) {
                double sumSqBlock = 0.0;
                uint32_t start = b * blockFrames;
                for (uint32_t i = start; i < start + blockFrames; i++) {
                    sumSqBlock += (double)mono[i] * mono[i];
                }
                double rms = std::sqrt(sumSqBlock / blockFrames);
                if (rms > 1e-10) {
                    blockRms.push_back(rms);
                }
            }

            if (blockRms.size() >= 2) {
                double maxRms = *std::max_element(blockRms.begin(), blockRms.end());
                double minRms = *std::min_element(blockRms.begin(), blockRms.end());
                if (minRms > 1e-10) {
                    result.dynamicRangeDB = static_cast<float>(
                        20.0 * std::log10(maxRms / minRms));
                }

                // Coefficient of variation
                double mean = 0.0;
                for (double r : blockRms) mean += r;
                mean /= blockRms.size();
                double var = 0.0;
                for (double r : blockRms) {
                    double d = r - mean;
                    var += d * d;
                }
                double stddev = std::sqrt(var / blockRms.size());
                result.dynamicRangeCoeffVar = mean > 1e-10
                    ? static_cast<float>(stddev / mean)
                    : 0.0f;
            }
        }
    }

    return result;
}

std::string AudioAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"rmsLevel\":" << rmsLevel;
    ss << ",\"peakLevel\":" << peakLevel;
    ss << ",\"rmsLeft\":" << rmsLeft;
    ss << ",\"rmsRight\":" << rmsRight;
    ss << ",\"isSilent\":" << (isSilent ? "true" : "false");
    ss << ",\"crestFactor\":" << crestFactor;
    ss << ",\"spectrum\":{";
    ss << "\"subBass\":" << spectrum[0];
    ss << ",\"bass\":" << spectrum[1];
    ss << ",\"lowMid\":" << spectrum[2];
    ss << ",\"mid\":" << spectrum[3];
    ss << ",\"highMid\":" << spectrum[4];
    ss << ",\"high\":" << spectrum[5];
    ss << "}";
    ss << ",\"duration\":" << duration;
    ss << ",\"dcOffset\":" << dcOffset;
    ss << ",\"clippedSampleCount\":" << clippedSampleCount;
    ss << ",\"clippedSamplePct\":" << clippedSamplePct;
    ss << ",\"zeroCrossingRate\":" << zeroCrossingRate;
    ss << ",\"stereoCorrelation\":" << stereoCorrelation;
    ss << ",\"stereoWidth\":" << stereoWidth;
    ss << ",\"spectralCentroid\":" << spectralCentroid;
    ss << ",\"spectralSpread\":" << spectralSpread;
    ss << ",\"spectralFlux\":" << spectralFlux;
    ss << ",\"spectralFluxMax\":" << spectralFluxMax;
    ss << ",\"spectralFlatness\":" << spectralFlatness;
    ss << ",\"spectralRolloff\":" << spectralRolloff;
    ss << ",\"onsetDensity\":" << onsetDensity;
    ss << ",\"onsetCount\":" << onsetCount;

    // LUFS fields: serialize -inf as null
    auto writeFloat = [&](const char* name, float val) {
        ss << ",\"" << name << "\":";
        if (std::isfinite(val)) ss << val;
        else ss << "null";
    };
    writeFloat("integratedLUFS", integratedLUFS);
    writeFloat("shortTermLUFS", shortTermLUFS);
    writeFloat("momentaryLUFS", momentaryLUFS);
    ss << ",\"truePeak\":" << truePeak;
    writeFloat("truePeakDBTP", truePeakDBTP);
    ss << ",\"loudnessRange\":" << loudnessRange;
    ss << ",\"pitchHz\":" << pitchHz;
    ss << ",\"pitchConfidence\":" << pitchConfidence;
    ss << ",\"pitchNote\":";
    if (pitchNote.empty()) ss << "null";
    else ss << "\"" << pitchNote << "\"";
    ss << ",\"pitchCents\":" << pitchCents;
    ss << ",\"harmonicToNoiseRatio\":" << harmonicToNoiseRatio;
    ss << ",\"dynamicRangeDB\":" << dynamicRangeDB;
    ss << ",\"dynamicRangeCoeffVar\":" << dynamicRangeCoeffVar;

    ss << "}";
    return ss.str();
}

} // namespace vivid
