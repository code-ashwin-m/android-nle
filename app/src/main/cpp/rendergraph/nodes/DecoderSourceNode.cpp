#include "rendergraph/nodes/DecoderSourceNode.h"

#include "core/Project.h"

namespace nle {

Track* DecoderSourceNode::TrackPtr() const {
    return project_->GetTimeline().FindTrack(trackId_);
}

}  // namespace nle
