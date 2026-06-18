#include "rt/app/RendererApp.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        rt::app::RendererApp app(argc > 0 ? argv[0] : "");
        return app.run();
    } catch (const std::exception& error) {
        std::cerr << "renderer failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
