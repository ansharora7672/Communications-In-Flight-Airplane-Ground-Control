#include <optional>

#include "client_app.h"
#include "client_options.h"
#include "socket_utils.h"

int main(int argc, char* argv[]) {
    const std::optional<client::ClientOptions> parsedOptions = client::parseClientOptions(argc, argv);
    if (!parsedOptions.has_value()) {
        return 1;
    }

    initSockets();
    client::ClientConsoleApp app(*parsedOptions);
    const int exitCode = app.run();
    cleanupSockets();
    return exitCode;
}
