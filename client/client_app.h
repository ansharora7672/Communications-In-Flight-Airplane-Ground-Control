#pragma once

#include "aircraft_client.h"
#include "client_options.h"

namespace client {

class ClientConsoleApp {
public:
    explicit ClientConsoleApp(const ClientOptions& options);

    int run();

private:
    AircraftClient client;
};

} // namespace client
