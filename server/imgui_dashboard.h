#pragma once

#include "imgui.h"

#include "dashboard_state.h"

ImVec4 stateColor(StateMachine::State state);
void renderDashboard(DashboardState& state);
