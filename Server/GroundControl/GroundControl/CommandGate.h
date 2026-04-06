#pragma once

#include <string>
#include "ServerState.h"

inline bool canDispatchCommand(ServerState state)
{
    switch (state)
    {
    case STATE_CONNECTED:
    case STATE_TELEMETRY:
    case STATE_LARGE_FILE_TRANSFER:
        return true;

    case STATE_DISCONNECTED:
    case STATE_HANDSHAKE_PENDING:
    case STATE_FAULT:
    default:
        return false;
    }
}

inline const char* commandGateReason(ServerState state)
{
    switch (state)
    {
    case STATE_DISCONNECTED:
        return "Command blocked: server is DISCONNECTED";
    case STATE_HANDSHAKE_PENDING:
        return "Command blocked: handshake not complete";
    case STATE_FAULT:
        return "Command blocked: server is in FAULT";
    case STATE_CONNECTED:
    case STATE_TELEMETRY:
    case STATE_LARGE_FILE_TRANSFER:
        return "Command allowed";
    default:
        return "Command blocked: unknown state";
    }
}