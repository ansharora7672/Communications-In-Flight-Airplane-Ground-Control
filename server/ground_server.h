#pragma once

#include <atomic>
#include <mutex>

#include "imgui_dashboard.h"
#include "logger.h"
#include "server_options.h"
#include "socket_utils.h"

namespace server {

struct SharedServerState {
    std::mutex mutex;
    DashboardState dashboard;
};

class GroundServer {
public:
    explicit GroundServer(const ServerOptions& options);
    ~GroundServer();

    GroundServer(const GroundServer&) = delete;
    GroundServer& operator=(const GroundServer&) = delete;

    int run();

private:
    bool openListenSocket();
    int runHeadless();
    int runDashboard();
    void closeListenSocket();

    ServerOptions options;
    Logger logger {"groundctrl"};
    SharedServerState sharedState;
    std::atomic<bool> running {true};
    SocketHandle listenSocket = INVALID_SOCK;
};

} // namespace server
