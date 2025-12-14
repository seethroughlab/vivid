// Vivid Network Addon
// Network communication for installations and remote control
//
// Operators:
//   UdpIn      - Receive UDP datagrams (hardware protocols, sensors)
//   UdpOut     - Send UDP datagrams
//   OscIn      - Receive OSC messages (TouchOSC, Max/MSP, etc.)
//   OscOut     - Send OSC messages
//   WebServer  - HTTP server with REST API and WebSocket support
//
// Usage:
//   #include <vivid/network/network.h>
//   using namespace vivid::network;

#pragma once

#include "udp_in.h"
#include "udp_out.h"
#include "osc_in.h"
#include "osc_out.h"
#include "web_server.h"
