#pragma once

#include <string>

#include "state_machine.h"

namespace server {

bool validateAircraftId(const std::string& aircraftId);
bool isVerifiedSessionState(StateMachine::State state);

} // namespace server
