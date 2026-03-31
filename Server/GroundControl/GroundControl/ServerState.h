#pragma once

enum ServerState {
    STATE_DISCONNECTED,            
    STATE_HANDSHAKE_PENDING,         
    STATE_CONNECTED,              
    STATE_TELEMETRY,                
    STATE_LARGE_FILE_TRANSFER,       
    STATE_FAULT               
};