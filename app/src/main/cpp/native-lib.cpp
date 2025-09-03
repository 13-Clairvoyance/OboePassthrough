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
            mWindow[i] = 0.5f - 0.5f * cosf(2.0f * M_PI * i / (mBufferSize - 1));
        }
        mWindowedInput.resize(mBufferSize);
        mFftOutput.resize(mBufferSize / 2 + 1);
        mFftCfg = kiss_fftr_alloc(mBufferSize, 0, nullptr, nullptr);
        mIfftCfg = kiss_fftr_alloc(mBufferSize, 1, nullptr, nullptr);
        mConversionBuffer.resize(mBufferSize);

        mOverlapBuffer.resize(mBufferSize / 2, 0.0f);
    }

    ~MicPassthrough() {
        stop();
        kiss_fftr_free(mFftCfg);
        kiss_fftr_free(mIfftCfg);
    }

    void start() {
        stop();

        mInputRingBuffer.resize(mBufferSize * 2, 0.0f);
        mRingWriteIndex = mRingReadIndex = mRingSize = 0;
        mOutputFIFO.clear();
        mOutputFIFO.reserve(mBufferSize * 8);  // avoid reallocation
        mInputReadBuffer.resize(std::max<int32_t>(mFramesPerBurst, 256));

        oboe::AudioStreamBuilder inBuilder;
        inBuilder.setDirection(oboe::Direction::Input)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                ->setChannelCount(oboe::ChannelCount::Mono)
                ->setSampleRate(mSampleRate) // device chooses sample rate
                ->setCallback(nullptr);

        oboe::Result r = inBuilder.openStream(mInputStream);
        if (r != oboe::Result::OK) {
            LOGI("Failed to open input: %s", oboe::convertToText(r));
            return;
        }

        // Align our internal rate to the real device rate
        mSampleRate = mInputStream->getSampleRate();
        mFramesPerBurst = mInputStream->getFramesPerBurst();

        // 2) Open OUTPUT stream (with callback = this)
        oboe::AudioStreamBuilder outBuilder;
        outBuilder.setDirection(oboe::Direction::Output)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                ->setChannelCount(oboe::ChannelCount::Mono)
                ->setSampleRate(mSampleRate)
                ->setFramesPerDataCallback(mFramesPerBurst) // helps alignment
                ->setCallback(this);

        r = outBuilder.openStream(mOutputStream);
        if (r != oboe::Result::OK) {
            LOGI("Failed to open output: %s", oboe::convertToText(r));
            return;
        }

        // 3) Start streams: input first, then output
        mInputStream->requestStart();
        mOutputStream->requestStart();

        // 4) Prep helper buffers
        mInputReadBuffer.resize(std::max<int32_t>(mFramesPerBurst, 256));
        mOutputFIFO.clear();

        LOGI("Duplex (two-stream) passthrough started at %d Hz, burst=%d",
             mSampleRate, mFramesPerBurst);
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
            oboe::AudioStream *stream, void *audioData, int32_t numFrames) override {

        float *out = static_cast<float*>(audioData);

        // 1) Read mic (non-blocking)
        if (mInputReadBuffer.size() < (size_t)numFrames) {
            mInputReadBuffer.resize(numFrames);
        }
        int32_t framesRead = 0;
        if (mInputStream) {
            auto res = mInputStream->read(mInputReadBuffer.data(), numFrames, 0);
            if (res) {
                framesRead = res.value();
            } else {
                framesRead = 0;
                // avoid logging every callback
            }
        }

        // 2) Write mic samples into ring buffer (real-time safe, O(1) per sample)
        for (int i = 0; i < framesRead; ++i) {
            mInputRingBuffer[mRingWriteIndex] = mInputReadBuffer[i];
            mRingWriteIndex = (mRingWriteIndex + 1) % mInputRingBuffer.size();

            if (mRingSize < (int)mInputRingBuffer.size()) {
                ++mRingSize;
            } else {
                // Overrun: advance read pointer to avoid overwriting unread data
                mRingReadIndex = (mRingReadIndex + 1) % mInputRingBuffer.size();
            }
        }

        // 3) Process full blocks while available (50% overlap)
        const int hop = mBufferSize / 2;
        while (mRingSize >= mBufferSize) {
            // copy block from ring to window buffer
            for (int n = 0; n < mBufferSize; ++n) {
                int idx = (mRingReadIndex + n) % mInputRingBuffer.size();
                mWindowedInput[n] = mInputRingBuffer[idx] * mWindow[n]; // window here
            }

            // FFT
            kiss_fftr(mFftCfg, mWindowedInput.data(), mFftOutput.data());

            // Filter (keep 125-18000 Hz)
            for (int k = 0; k < static_cast<int>(mFftOutput.size()); ++k) {
                float freq = (static_cast<float>(k) * mSampleRate) / mBufferSize;
                float gain = (freq < 125.0f || freq > 18000.0f) ? 0.0f : 1.0f;
                mFftOutput[k].r *= gain;
                mFftOutput[k].i *= gain;
            }

            // IFFT
            kiss_fftri(mIfftCfg, mFftOutput.data(), mConversionBuffer.data());

            // Normalize
            for (int i = 0; i < mBufferSize; ++i) {
                mConversionBuffer[i] /= mBufferSize;
            }

            // Overlap-add (50% hop)
            int half = hop;
            for (int i = 0; i < half; ++i) {
                mConversionBuffer[i] += mOverlapBuffer[i];
            }

            // push first half to output FIFO
            mOutputFIFO.insert(mOutputFIFO.end(),
                               mConversionBuffer.begin(),
                               mConversionBuffer.begin() + half);

            // save second half to overlap buffer
            std::copy(mConversionBuffer.begin() + half,
                      mConversionBuffer.end(),
                      mOverlapBuffer.begin());

            // advance read index & reduce available size by hop
            mRingReadIndex = (mRingReadIndex + hop) % mInputRingBuffer.size();
            mRingSize -= hop;
        }

        // 4) Deliver to output (from FIFO). If insufficient, zero-fill
        int available = static_cast<int>(mOutputFIFO.size());
        int toCopy = std::min(available, numFrames);
        if (toCopy > 0) {
            std::copy(mOutputFIFO.begin(), mOutputFIFO.begin() + toCopy, out);
            // remove copied samples (this is O(n) per erase; for production replace FIFO with ring)
            mOutputFIFO.erase(mOutputFIFO.begin(), mOutputFIFO.begin() + toCopy);
        }
        if (toCopy < numFrames) {
            std::fill(out + toCopy, out + numFrames, 0.0f);
        }

        return oboe::DataCallbackResult::Continue;
    }



private:
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    const int32_t mBufferSize;
    int mSampleRate;
    std::vector<float> mWindow;
    std::vector<float> mWindowedInput;
    std::vector<kiss_fft_cpx> mFftOutput;
    std::vector<float> mConversionBuffer;
    std::vector<float> mOverlapBuffer;
    kiss_fftr_cfg mFftCfg;
    kiss_fftr_cfg mIfftCfg;
    std::vector<float> mInputReadBuffer;   // temp mic reads per callback
    std::vector<float> mOutputFIFO;
    int32_t mFramesPerBurst = 0;
    int mAccumWriteIndex;
    int32_t mAccumSize;
    std::vector<float> mInputRingBuffer;
    int mRingWriteIndex = 0;
    int mRingReadIndex = 0;
    int mRingSize = 0;

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