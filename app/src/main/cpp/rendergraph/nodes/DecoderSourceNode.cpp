#include "rendergraph/nodes/DecoderSourceNode.h"

#include "core/Project.h"

namespace nle {

Track* DecoderSourceNode::TrackPtr() const {
    return project_->GetTimeline().FindTrack(trackId_);
}

Decoder* DecoderSourceNode::DecoderForSource(MediaSourceId sourceId){
    auto it = decoders_.find(sourceId.value);
    if (it != decoders_.end())
        return it->second.decoder.get();

    MediaSource* source = project_->FindMedia(sourceId);
    if (!source)
        return nullptr;

    auto decoder = std::make_unique<Decoder>();

    if (!decoder->Open(source->Uri()))
        return nullptr;

    if (!AttachSurfaceTexture(*decoder, sourceId))
        return nullptr;

    Decoder* ptr = decoder.get();
    decoders_[sourceId.value].decoder = std::move(decoder);

    return ptr;
}

}  // namespace nle
