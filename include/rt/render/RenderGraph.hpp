#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rt::render {

struct GraphResource {
    std::string name;
};

struct PassContext {
    std::string_view passName;
};

using PassExecuteFn = std::function<void(const PassContext&)>;

class RenderGraph {
public:
    void addPass(std::string name,
                 std::vector<GraphResource> reads,
                 std::vector<GraphResource> writes,
                 PassExecuteFn execute);

    void compile();
    void execute() const;
    [[nodiscard]] const std::vector<std::string>& executionOrder() const noexcept;

private:
    struct Pass {
        std::string name;
        std::vector<GraphResource> reads;
        std::vector<GraphResource> writes;
        PassExecuteFn execute;
    };

    std::vector<Pass> passes_;
    std::vector<std::string> executionOrder_;
    std::unordered_map<std::string, std::size_t> passIndexByName_;
};

} // namespace rt::render
