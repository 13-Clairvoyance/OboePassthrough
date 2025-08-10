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
    // CHANGED: The constructor now takes the sample rate.
    MicPassthrough(int32_t bufferSize, int32_t sampleRate) :
            mBufferSize(bufferSize),
            mSampleRate(sampleRate) {

        // Your preparation logic was excellent and is kept here.
        mWindow.resize(mBufferSize);
        for (int i = 0; i < mBufferSize; ++i) {
            mWindow[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * static_cast<float>(i) / (mBufferSize - 1));
        }
        mWindowedInput.resize(mBufferSize);
        mFftOutput.resize(mBufferSize / 2 + 1);
        mFftCfg = kiss_fftr_alloc(mBufferSize, 0, nullptr, nullptr);
        mIfftCfg = kiss_fftr_alloc(mBufferSize, 1, nullptr, nullptr);

        // ADDED: Pre-allocate the output buffer to avoid real-time allocation.
        mConversionBuffer.resize(mBufferSize);
    }

    ~MicPassthrough() {
        // Your cleanup logic was correct.
        stop(); // Ensure streams are stopped before freeing resources.
        kiss_fftr_free(mFftCfg);
        kiss_fftr_free(mIfftCfg);
    }

    void start() {
        stop(); // Ensure clean state before starting.

        oboe::AudioStreamBuilder builder;

        // --- CHANGED: Configure BOTH streams first, then open them. ---
        builder.setDirection(oboe::Direction::Input)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                        // CHANGED: We will process mono for simplicity.
                ->setChannelCount(oboe::ChannelCount::Mono)
                ->setSampleRate(mSampleRate)
                        // CHANGED: The callback is now set on the INPUT stream.
                ->setCallback(this);

        oboe::Result result = builder.openStream(mInputStream);
        if (result != oboe::Result::OK) {
            LOGI("Failed to open input stream. Error: %s", oboe::convertToText(result));
            return;
        }

        // Now configure the output stream.
        builder.setDirection(oboe::Direction::Output)
                        // The output callback is removed.
                ->setCallback(nullptr);

        result = builder.openStream(mOutputStream);
        if (result != oboe::Result::OK) {
            LOGI("Failed to open output stream. Error: %s", oboe::convertToText(result));
            return;
        }

        // Start both streams.
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

    // --- CHANGED: This is now the INPUT stream's callback. ---
    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *oboeStream,
            void *audioData,
            int32_t numFrames) override {

        // This is the incoming MONO audio data from the microphone.
        auto* inputFloats = static_cast<float*>(audioData);

        // --- This is the new, correct processing chain ---

        // 1. Apply Hamming window to the input signal.
        for (int i = 0; i < numFrames; ++i) {
            mWindowedInput[i] = inputFloats[i] * mWindow[i];
        }

        // Logging windowed input for debugging.
        //LOGI("Input[0]: %f, Windowed[0]: %f", inputFloats[0], mWindowedInput[0]);
        //LOGI("Input[1]: %f, Windowed[1]: %f", inputFloats[1], mWindowedInput[1]);

        // 2. Perform FFT
        kiss_fftr(mFftCfg, mWindowedInput.data(), mFftOutput.data());

        // 3. Apply gain to desired frequencies
        // --- CHANGED: This logic is updated to boost high frequencies for speech clarity ---
        for (int i = 0; i < mFftOutput.size(); ++i) {
            float freq = static_cast<float>(i * mSampleRate) / mBufferSize;
            float gain = 1.0f; // Default: no change

            // Frequencies from 2500Hz to 6000Hz are crucial for consonant sounds ('s', 't', 'f')
            // that make speech intelligible.
            if (freq > 2500.0f && freq < 6000.0f) {
                // Let's make them 4 times louder. You can experiment with this value.
                gain = 10.0f;
            }
            mFftOutput[i].r *= gain;
            mFftOutput[i].i *= gain;
        }

        // 4. Perform inverse FFT (IFFT)
        // The result is placed into our temporary conversion buffer.
        kiss_fftri(mIfftCfg, mFftOutput.data(), mConversionBuffer.data());

        // Inside onAudioReady, after the kiss_fftr call

        /* Testing FFT
        int frequency = 1000;
        int targetBin = frequency * mBufferSize / mSampleRate; // e.g., 1000 * 1024 / 48000 = bin 21

        LOGI("--- FFT Output ---");
        // Log the bins around your target
        for (int i = targetBin - 3; i < targetBin + 4; ++i) {
            // Calculate magnitude: sqrt(real^2 + imag^2)
            float magnitude = sqrt(mFftOutput[i].r * mFftOutput[i].r + mFftOutput[i].i * mFftOutput[i].i);
            LOGI("Bin %d: Magnitude = %f", i, magnitude);
        }
        */
        // 5. Normalize the output
        for (int i = 0; i < numFrames; ++i) {
            mConversionBuffer[i] /= numFrames;
        }

        // 6. Write the processed MONO audio to the output stream.
        // Oboe will automatically handle writing the mono data to both stereo channels if needed.
        if (mOutputStream) {
            mOutputStream->write(mConversionBuffer.data(), numFrames, 0);
        }

        return oboe::DataCallbackResult::Continue;
    }

private:
    // --- ADDED: Member variables for streams and configs ---
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::shared_ptr<oboe::AudioStream> mOutputStream;

    const int32_t mBufferSize;
    const int32_t mSampleRate;

    std::vector<float> mWindow;
    std::vector<float> mWindowedInput;
    std::vector<kiss_fft_cpx> mFftOutput;
    std::vector<float> mConversionBuffer; // Buffer to hold the IFFT result.

    kiss_fftr_cfg mFftCfg;
    kiss_fftr_cfg mIfftCfg;
};

// --- JNI ---
// ADDED: A global pointer to manage the object's lifecycle.
std::unique_ptr<MicPassthrough> passthroughEngine = nullptr;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_MainActivity_startPassthrough(JNIEnv *, jobject) {
// For simplicity, we hardcode buffer size and sample rate.
// A more robust app might get these from the AudioManager.
if (!passthroughEngine) {
passthroughEngine = std::make_unique<MicPassthrough>(1024, 48000);
}
passthroughEngine->start();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_MainActivity_stopPassthrough(JNIEnv *, jobject) {
if (passthroughEngine) {
passthroughEngine->stop();
// Optional: uncomment to destroy the engine on stop.
// passthroughEngine.reset();
}
}
