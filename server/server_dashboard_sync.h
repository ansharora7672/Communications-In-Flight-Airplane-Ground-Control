#pragma once

#include <string>

#include "client_session.h"
#include "dashboard_state.h"

void appendDashboardLogEntry(DashboardState& dashboard, const std::string& entry);
void syncDashboardAircraft(
    DashboardState& dashboard,
    const server::ClientSession& session,
    const std::string& alertMessage = std::string());
