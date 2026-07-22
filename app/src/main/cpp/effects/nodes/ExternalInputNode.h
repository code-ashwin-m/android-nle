// ExternalInputNode.h
//
// A RenderGraph's only way to get data in is a node with zero input edges
// whose Process() produces something. Every existing source is a
// DecoderSourceNode reading from a decoder. This is the other kind of
// source: one whose Frame is pushed in from outside, by whatever C++ code
// owns the RenderGraph instance, immediately before calling Execute().
//
// This is what lets a RenderGraph be used as an embedded sub-pipeline inside
// another RenderNode's Process() -- see
// rendergraph/nodes/PackagedEffectSlotNode.h, which owns one small
// RenderGraph per active packaged effect, seeds it with the upstream frame
// via SetFrame(), and calls Execute() on it as an implementation detail.
// RenderGraph itself needed no changes to support this -- it already has no
// concept of where a graph's non-connected nodes get their data from.

#pragma once

#include "rendergraph/RenderNode.h"

namespace nle {

class ExternalInputNode : public RenderNode {
public:
    std::string Name() const override { return "ExternalInput"; }

    // Called by the owning node right before RenderGraph::Execute() on the
    // graph this belongs to -- see PackagedEffectSlotNode::Process().
    void SetFrame(Frame frame) { frame_ = frame; }

    Frame Process(const std::vector<Frame>& /*inputs*/, const RenderContext& /*context*/) override { return frame_; }

private:
    Frame frame_;
};

}  // namespace nle
