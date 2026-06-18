#include "rt/render/RenderGraph.hpp"

#include <queue>
#include <stdexcept>
#include <utility>

namespace rt::render {

void RenderGraph::addPass(std::string name,
                          std::vector<GraphResource> reads,
                          std::vector<GraphResource> writes,
                          PassExecuteFn execute)
{
    if (name.empty()) {
        throw std::invalid_argument("render graph pass name cannot be empty");
    }
    if (!execute) {
        throw std::invalid_argument("render graph pass requires an execute callback");
    }
    if (passIndexByName_.contains(name)) {
        throw std::invalid_argument("duplicate render graph pass: " + name);
    }

    const std::size_t index = passes_.size();
    passIndexByName_.emplace(name, index);
    passes_.push_back({std::move(name), std::move(reads), std::move(writes), std::move(execute)});
}

void RenderGraph::compile()
{
    std::unordered_map<std::string, std::size_t> lastWriterByResource;
    std::vector<std::vector<std::size_t>> edges(passes_.size());
    std::vector<std::size_t> incoming(passes_.size(), 0);

    for (std::size_t passIndex = 0; passIndex < passes_.size(); ++passIndex) {
        for (const auto& resource : passes_[passIndex].reads) {
            const auto writer = lastWriterByResource.find(resource.name);
            if (writer == lastWriterByResource.end()) {
                continue;
            }
            edges[writer->second].push_back(passIndex);
            ++incoming[passIndex];
        }

        for (const auto& resource : passes_[passIndex].writes) {
            lastWriterByResource[resource.name] = passIndex;
        }
    }

    std::queue<std::size_t> ready;
    for (std::size_t passIndex = 0; passIndex < incoming.size(); ++passIndex) {
        if (incoming[passIndex] == 0) {
            ready.push(passIndex);
        }
    }

    executionOrder_.clear();
    while (!ready.empty()) {
        const std::size_t passIndex = ready.front();
        ready.pop();
        executionOrder_.push_back(passes_[passIndex].name);

        for (const std::size_t dependent : edges[passIndex]) {
            --incoming[dependent];
            if (incoming[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    if (executionOrder_.size() != passes_.size()) {
        throw std::runtime_error("render graph contains a dependency cycle");
    }
}

void RenderGraph::execute() const
{
    if (executionOrder_.size() != passes_.size()) {
        throw std::runtime_error("render graph must be compiled before execution");
    }

    for (const auto& passName : executionOrder_) {
        const auto index = passIndexByName_.find(passName);
        if (index == passIndexByName_.end()) {
            throw std::runtime_error("compiled render graph references missing pass: " + passName);
        }
        passes_[index->second].execute({.passName = passes_[index->second].name});
    }
}

const std::vector<std::string>& RenderGraph::executionOrder() const noexcept
{
    return executionOrder_;
}

} // namespace rt::render
