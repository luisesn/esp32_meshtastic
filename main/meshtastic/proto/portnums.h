#pragma once
#include <stdint.h>

/* Meshtastic PortNum enum (subset used in this firmware).
 * Values from meshtastic/protobufs/meshtastic/portnums.proto */
typedef enum {
    PORTNUM_UNKNOWN_APP      = 0,
    PORTNUM_TEXT_MESSAGE_APP = 1,
    PORTNUM_REMOTE_HARDWARE  = 2,
    PORTNUM_POSITION_APP     = 3,
    PORTNUM_NODEINFO_APP     = 4,
    PORTNUM_ROUTING_APP      = 5,
    PORTNUM_TELEMETRY_APP    = 67,
    PORTNUM_PRIVATE_APP      = 256,
} meshtastic_PortNum;

const char *portnum_name(meshtastic_PortNum p);
