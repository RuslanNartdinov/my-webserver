#include "webserv/App.hpp"
#include "webserv/Log.hpp"
#include "webserv/Version.hpp"
#include <iostream>

static void printUsage(const char* argv0) {
    std::cout << "Usage: " << argv0 << " [config_path]\n"
              << "Default: examples/basic.conf\n";
}

int main(int argc, char** argv) {
    std::string configPath = "examples/basic.conf";
    if (argc >= 2) {
        std::string arg(argv[1]);
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        configPath = arg;
    }

    ws::App app;
    return app.run(configPath);
}
