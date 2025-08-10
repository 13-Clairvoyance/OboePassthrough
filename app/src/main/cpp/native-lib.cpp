#include <jni.h>
#include <oboe/Oboe.h>
#include <android/log.h>
#include <cmath>
#include <memory>
#include <vector>

#include "kiss_fft.h"
#include "kiss_fftr.h"

#define TAG "OboeNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

class MicPassthrough : public oboe::AudioStreamCallback {
public:
    MicPassthrough(int32_t bufferSize, int32_t sampleRate) :
            mBufferSize(bufferSize),
            mSampleRate(sampleRate) {
        mWindow.resize(mBufferSize);
        for (int i = 0; i < mBufferSize; ++i) {
            mWindow[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * static_cast<float>(i) / (mBufferSize - 1));
        }
        mWindowedInput.resize(mBufferSize);
        mFftOutput.resize(mBufferSize / 2 + 1);
        mFftCfg = kiss_fftr_alloc(mBufferSize, 0, nullptr, nullptr);
        mIfftCfg = kiss_fftr_alloc(mBufferSize, 1, nullptr, nullptr);
        mConversionBuffer.resize(mBufferSize);
    }

    ~MicPassthrough() {
        stop();
        kiss_fftr_free(mFftCfg);
        kiss_fftr_free(mIfftCfg);
    }

    void start() {
        stop();

        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Input)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                ->setChannelCount(oboe::ChannelCount::Mono)
                ->setSampleRate(mSampleRate)
                ->setCallback(this);

        oboe::Result result = builder.openStream(mInputStream);
        if (result != oboe::Result::OK) {
            LOGI("Failed to open input stream. Error: %s", oboe::convertToText(result));
            return;
        }

        builder.setDirection(oboe::Direction::Output)
                ->setCallback(nullptr);

        result = builder.openStream(mOutputStream);
        if (result != oboe::Result::OK) {
            LOGI("Failed to open output stream. Error: %s", oboe::convertToText(result));
            return;
        }

        mInputStream->requestStart();
        mOutputStream->requestStart();
        LOGI("Passthrough started");
    }

    void stop() {
        if (mInputStream) {
            mInputStream->stop();
            mInputStream->close();
            mInputStream.reset();
        }
        if (mOutputStream) {
            mOutputStream->stop();
            mOutputStream->close();
            mOutputStream.reset();
        }
        LOGI("Passthrough stopped");
    }

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *oboeStream,
            void *audioData,
            int32_t numFrames) override {
        auto* inputFloats = static_cast<float*>(audioData);

        for (int i = 0; i < numFrames; ++i) {
            mWindowedInput[i] = inputFloats[i] * mWindow[i];
        }

        kiss_fftr(mFftCfg, mWindowedInput.data(), mFftOutput.data());

        for (int i = 0; i < mFftOutput.size(); ++i) {
            float freq = static_cast<float>(i * mSampleRate) / mBufferSize;
            float gain = 1.0f;
            if (freq > 2500.0f && freq < 6000.0f) {
                gain = 10.0f;
            }
            mFftOutput[i].r *= gain;
            mFftOutput[i].i *= gain;
        }

        kiss_fftri(mIfftCfg, mFftOutput.data(), mConversionBuffer.data());

        for (int i = 0; i < numFrames; ++i) {
            mConversionBuffer[i] /= numFrames;
        }

        if (mOutputStream) {
            // --- ADDED: Diagnostic Logging ---
            // 1. Log the first sample of the buffer we are about to play.
            // This tells us if the audio is silent (all zeros).
            LOGI("Output buffer sample[0]: %f", mConversionBuffer[0]);

            // 2. Write to the output stream and log the result.
            // This tells us if the write operation itself is failing.
            auto result = mOutputStream->write(mConversionBuffer.data(), numFrames, 0);
            if (!result) {
                LOGI("Error writing to output stream: %s", oboe::convertToText(result.error()));
            }
        }
        return oboe::DataCallbackResult::Continue;
    }

private:
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    const int32_t mBufferSize;
    const int32_t mSampleRate;
    std::vector<float> mWindow;
    std::vector<float> mWindowedInput;
    std::vector<kiss_fft_cpx> mFftOutput;
    std::vector<float> mConversionBuffer;
    kiss_fftr_cfg mFftCfg;
    kiss_fftr_cfg mIfftCfg;
};

std::unique_ptr<MicPassthrough> passthroughEngine = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_AudioProcessingService_startPassthrough(JNIEnv *, jobject) {
    if (!passthroughEngine) {
        passthroughEngine = std::make_unique<MicPassthrough>(1024, 48000);
    }
    passthroughEngine->start();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_AudioProcessingService_stopPassthrough(JNIEnv *, jobject) {
    if (passthroughEngine) {
        passthroughEngine->stop();
    }
}
