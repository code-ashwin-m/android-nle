#include "encode/Encoder.h"

#include <android/log.h>
#include <fcntl.h>
#include <media/NdkMediaFormat.h>

#define LOG_TAG "Encoder"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace nle {

bool Encoder::Configure(const EncoderConfig& config) {
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, config.mimeType.c_str());
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, config.width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, config.height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, config.bitrateBps);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<int32_t>(config.fps));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, config.iFrameIntervalSeconds);
    // COLOR_FormatSurface -- required when the encoder will receive frames
    // via a Surface rather than ByteBuffers.
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7f000789);

    codec_ = AMediaCodec_createEncoderByType(config.mimeType.c_str());
    if (!codec_) {
        LOGE("No encoder available for mime '%s'", config.mimeType.c_str());
        AMediaFormat_delete(format);
        return false;
    }

    media_status_t status = AMediaCodec_configure(codec_, format, nullptr, nullptr,
                                                   AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK) {
        LOGE("Encoder configure failed: %d", status);
        return false;
    }

    status = AMediaCodec_createInputSurface(codec_, &inputSurface_);
    if (status != AMEDIA_OK) {
        LOGE("createInputSurface failed: %d", status);
        return false;
    }

    outputFd_ = open(config.outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outputFd_ < 0) {
        LOGE("Failed to open output path '%s'", config.outputPath.c_str());
        return false;
    }
    muxer_ = AMediaMuxer_new(outputFd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);

    AMediaCodec_start(codec_);
    return true;
}

void Encoder::SignalEndOfStream() {
    AMediaCodec_signalEndOfInputStream(codec_);
}

bool Encoder::DrainOnce() {
    if (finalized_) return false;

    AMediaCodecBufferInfo info;
    int32_t index = AMediaCodec_dequeueOutputBuffer(codec_, &info, /*timeoutUs*/ 10000);

    if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* format = AMediaCodec_getOutputFormat(codec_);
        muxerVideoTrack_ = AMediaMuxer_addTrack(muxer_, format);
        AMediaFormat_delete(format);
        AMediaMuxer_start(muxer_);
        muxerStarted_ = true;
        return true;
    }

    if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        return true;  // nothing ready yet; caller will call again shortly
    }

    if (index < 0) {
        return true;  // AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED or similar; ignorable with this API level
    }

    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        AMediaCodec_releaseOutputBuffer(codec_, index, false);
        Finalize();
        return false;
    }

    if (muxerStarted_ && info.size > 0) {
        size_t bufferCapacity = 0;
        uint8_t* buffer = AMediaCodec_getOutputBuffer(codec_, index, &bufferCapacity);
        if (buffer) {
            AMediaMuxer_writeSampleData(muxer_, muxerVideoTrack_, buffer, &info);
        }
    }
    AMediaCodec_releaseOutputBuffer(codec_, index, false);
    return true;
}

void Encoder::Finalize() {
    if (finalized_) return;
    finalized_ = true;
    if (muxerStarted_) AMediaMuxer_stop(muxer_);
    if (muxer_) AMediaMuxer_delete(muxer_);
    if (codec_) {
        AMediaCodec_stop(codec_);
        AMediaCodec_delete(codec_);
    }
    if (outputFd_ >= 0) close(outputFd_);
}

}  // namespace nle
