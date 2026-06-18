#include "rt/render/RenderGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    rt::render::RenderGraph graph;
    std::vector<std::string> executed;

    graph.addPass("upload", {}, {{"mesh-buffer"}}, [&](const rt::render::PassContext& ctx) {
        executed.emplace_back(ctx.passName);
    });
    graph.addPass("build-blas", {{"mesh-buffer"}}, {{"blas"}}, [&](const rt::render::PassContext& ctx) {
        executed.emplace_back(ctx.passName);
    });
    graph.addPass("build-tlas", {{"blas"}}, {{"tlas"}}, [&](const rt::render::PassContext& ctx) {
        executed.emplace_back(ctx.passName);
    });

    graph.compile();
    graph.execute();

    const std::vector<std::string> expected = {"upload", "build-blas", "build-tlas"};
    if (executed != expected) {
        std::cerr << "unexpected render graph order\n";
        for (const auto& pass : executed) {
            std::cerr << pass << '\n';
        }
        return EXIT_FAILURE;
    }

    std::cout << "render graph dependency order verified\n";
    return EXIT_SUCCESS;
}
