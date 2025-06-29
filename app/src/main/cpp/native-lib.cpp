#include <jni.h>
#include <oboe/Oboe.h>
#include <android/log.h>
#include <cmath>
#include <memory>
#include <vector>

#define TAG "OboeNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

class MicPassthrough : public oboe::AudioStreamCallback {
public:
    std::shared_ptr<oboe::AudioStream> inputStream;
    std::shared_ptr<oboe::AudioStream> outputStream;
    std::vector<float> inputBuffer;
    float noiseThreshold = 0.01f;

    void start() {
        stop();

        oboe::AudioStreamBuilder inputBuilder, outputBuilder;

        inputBuilder.setDirection(oboe::Direction::Input)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                ->setChannelCount(oboe::ChannelCount::Stereo)
                ->setSampleRate(48000);

        inputBuilder.openStream(inputStream);
        inputStream->start();

        outputBuilder.setDirection(oboe::Direction::Output)
                ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                ->setSharingMode(oboe::SharingMode::Exclusive)
                ->setFormat(oboe::AudioFormat::Float)
                ->setChannelCount(inputStream->getChannelCount())
                ->setSampleRate(inputStream->getSampleRate())
                ->setCallback(this);

        outputBuilder.openStream(outputStream);
        outputStream->requestStart();

        LOGI("Passthrough started");
    }

    void stop() {
        if (inputStream) {
            inputStream->stop();
            inputStream->close();
            inputStream.reset();
        }

        if (outputStream) {
            outputStream->stop();
            outputStream->close();
            outputStream.reset();
        }

        inputBuffer.clear();
        LOGI("Passthrough stopped");
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        if (!inputStream || inputStream->getState() != oboe::StreamState::Started) return oboe::DataCallbackResult::Continue;

        int channelCount = inputStream->getChannelCount();
        if (inputBuffer.size() < numFrames * channelCount) {
            inputBuffer.resize(numFrames * channelCount);
        }

        inputStream->read(inputBuffer.data(), numFrames, 0);
        float *output = static_cast<float *>(audioData);

        for (int i = 0; i < numFrames * channelCount; ++i) {
            float sample = inputBuffer[i];
            output[i] = (fabs(sample) > noiseThreshold) ? sample : 0.0f;
        }

        return oboe::DataCallbackResult::Continue;
    }
};

MicPassthrough passthrough;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_MainActivity_startPassthrough(JNIEnv *, jobject) {
    passthrough.start();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_oboepassthrough_MainActivity_stopPassthrough(JNIEnv *, jobject) {
    passthrough.stop();
}
