// RenderGraph.h
//
// "Preview and export MUST use the exact same render graph. Only the output
// node changes." This class is what makes that literal, not aspirational:
// RenderGraph is a generic DAG of RenderNodes with no concept of "preview"
// or "export" anywhere in it. PlaybackEngine builds one graph instance and
// swaps only which node is wired up as the terminal output (an
// OpenGLPreviewOutputNode vs an EncoderSurfaceOutputNode, both in
// rendergraph/nodes/OutputNode.h) -- every upstream node (decode, color
// convert, brightness, composite) runs byte-for-byte identically either
// way, which is what guarantees exported video matches what was previewed.
//
// Execution model: nodes are topologically sorted once whenever the graph
// is rebuilt (i.e. on timeline structure changes -- adding an effect, not
// every frame), and re-executed in that fixed order on every frame without
// re-sorting. Rebuilding the graph is relatively rare (an editing action);
// executing it happens every rendered frame, so the hot path is a flat
// vector iteration, not a sort.

#pragma once

#include <cassert>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "rendergraph/RenderNode.h"

namespace nle {

using NodeHandle = int;
constexpr NodeHandle kInvalidNode = -1;

class RenderGraph {
public:
    NodeHandle AddNode(std::unique_ptr<RenderNode> node) {
        NodeHandle handle = static_cast<NodeHandle>(nodes_.size());
        nodes_.push_back(std::move(node));
        edges_.emplace_back();  // inputs for this node, filled by Connect()
        dirty_ = true;
        return handle;
    }

    // Declares that `to` consumes `from`'s output as one of its inputs.
    // Input order matters for nodes like CompositeNode (bottom track first)
    // so edges are appended in call order, not sorted.
    void Connect(NodeHandle from, NodeHandle to) {
        assert(from >= 0 && from < static_cast<NodeHandle>(nodes_.size()));
        assert(to >= 0 && to < static_cast<NodeHandle>(nodes_.size()));
        edges_[to].push_back(from);
        dirty_ = true;
    }

    void SetOutputNode(NodeHandle node) { outputNode_ = node; }

    void AttachAll(const RenderContext& context) {
        for (auto& node : nodes_) node->OnAttach(context);
    }

    void DetachAll() {
        for (auto& node : nodes_) node->OnDetach();
    }

    // Runs every node in dependency order and returns the output node's
    // frame. Intermediate frames are cached only for the duration of this
    // single call (executionCache_ is cleared each time) -- nodes that want
    // cross-frame caching (e.g. a decoder holding a frame queue) do that
    // internally, not through the graph.
    Frame Execute(const RenderContext& context) {
        if (dirty_) {
            topoOrder_ = TopologicalSort();
            dirty_ = false;
        }
        if (outputNode_ == kInvalidNode) return Frame{};

        executionCache_.assign(nodes_.size(), Frame{});
        std::vector<bool> computed(nodes_.size(), false);

        for (NodeHandle handle : topoOrder_) {
            std::vector<Frame> inputs;
            inputs.reserve(edges_[handle].size());
            for (NodeHandle dep : edges_[handle]) {
                inputs.push_back(executionCache_[dep]);
            }
            executionCache_[handle] = nodes_[handle]->Process(inputs, context);
            computed[handle] = true;
        }

        return executionCache_[outputNode_];
    }

    RenderNode* GetNode(NodeHandle handle) const { return nodes_[handle].get(); }
    size_t NodeCount() const { return nodes_.size(); }

private:
    std::vector<NodeHandle> TopologicalSort() const {
        std::vector<int> inDegree(nodes_.size(), 0);
        for (auto& deps : edges_) {
            // inDegree here is "how many nodes still need to run before me"
        }
        std::vector<std::vector<NodeHandle>> dependents(nodes_.size());
        for (NodeHandle to = 0; to < static_cast<NodeHandle>(edges_.size()); ++to) {
            for (NodeHandle from : edges_[to]) {
                dependents[from].push_back(to);
                inDegree[to]++;
            }
        }

        std::vector<NodeHandle> queue;
        for (NodeHandle n = 0; n < static_cast<NodeHandle>(nodes_.size()); ++n) {
            if (inDegree[n] == 0) queue.push_back(n);
        }

        std::vector<NodeHandle> order;
        order.reserve(nodes_.size());
        size_t head = 0;
        while (head < queue.size()) {
            NodeHandle n = queue[head++];
            order.push_back(n);
            for (NodeHandle dependent : dependents[n]) {
                if (--inDegree[dependent] == 0) queue.push_back(dependent);
            }
        }

        if (order.size() != nodes_.size()) {
            throw std::runtime_error("RenderGraph: cycle detected -- graph must be a DAG");
        }
        return order;
    }

    std::vector<std::unique_ptr<RenderNode>> nodes_;
    std::vector<std::vector<NodeHandle>> edges_;  // edges_[to] = list of `from` node handles
    NodeHandle outputNode_ = kInvalidNode;

    bool dirty_ = true;
    std::vector<NodeHandle> topoOrder_;
    std::vector<Frame> executionCache_;
};

}  // namespace nle
