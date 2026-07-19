// Encoder.h
//
// Implements the spec's export pipeline: "Decoder -> Render Graph ->
// Encoder -> Muxer -> MP4. No duplicate rendering code." The "no duplicate
// rendering code" half is satisfied by RenderGraph/EncoderOutputNode
// (rendergraph/nodes/OutputNode.h) reusing the same graph as preview; this
// class's job is strictly the encode+mux half: configure AMediaCodec in
// surface-input mode so EncoderOutputNode can draw directly into it via
// GL (zero-copy -- the rendered frame never touches a ByteBuffer), then
// drain the resulting encoded Annex-B/HEVC bitstream into an AMediaMuxer
// writing an MP4 container.
//
// Surface-input mode specifically (AMediaCodec_createInputSurface) is what
// makes "reuse the same renderer" for export possible at all: with a
// ByteBuffer-input encoder, EncoderOutputNode would need to read pixels
// back from the GPU (glReadPixels) and hand raw bytes to the codec --
// slow, and a second code path from what preview does. Surface input lets
// the exact same DrawFullscreenQuad call from preview also serve export.

#pragma once

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>

#include <atomic>
#include <string>

struct ANativeWindow;

namespace nle {

struct EncoderConfig {
    int width = 1080;
    int height = 1920;
    int bitrateBps = 12'000'000;
    double fps = 30.0;
    int iFrameIntervalSeconds = 2;
    std::string outputPath;
    std::string mimeType = "video/avc";  // H.264 for Phase 1; HEVC/AV1 configurable later
};

class Encoder {
public:
    bool Configure(const EncoderConfig& config);

    // Returned window is what a GLContext (render/GLContext.h) is
    // initialized against for the export pass -- see EncoderOutputNode,
    // which draws into this exact surface.
    ANativeWindow* GetInputSurface() const { return inputSurface_; }

    // Marks that no more frames will be submitted; the codec will emit a
    // final buffer with AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM once drained.
    void SignalEndOfStream();

    // Pulls exactly one available encoded buffer (if any) and writes it to
    // the muxer. Called in a loop by EncoderThread; returns false once
    // end-of-stream has been drained and muxing is finalized.
    bool DrainOnce();

    void Finalize();

private:
    AMediaCodec* codec_ = nullptr;
    AMediaMuxer* muxer_ = nullptr;
    ANativeWindow* inputSurface_ = nullptr;
    int muxerVideoTrack_ = -1;
    bool muxerStarted_ = false;
    bool finalized_ = false;
    int outputFd_ = -1;
};

}  // namespace nle
