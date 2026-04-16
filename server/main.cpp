#include <optional>

#include "ground_server.h"
#include "server_options.h"
#include "socket_utils.h"

int main(int argc, char* argv[]) {
    const std::optional<server::ServerOptions> parsedOptions = server::parseServerOptions(argc, argv);
    if (!parsedOptions.has_value()) {
        return 1;
    }

    initSockets();
    server::GroundServer app(*parsedOptions);
    const int exitCode = app.run();
    cleanupSockets();
    return exitCode;
}
