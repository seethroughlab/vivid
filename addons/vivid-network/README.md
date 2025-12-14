# vivid-network

Network communication for installations and remote control.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

| Operator | Description |
|----------|-------------|
| `UDPIn` | Receive UDP packets |
| `UDPOut` | Send UDP packets |
| `OSCIn` | Receive OSC messages |
| `OSCOut` | Send OSC messages |
| `WebServer` | HTTP/WebSocket server for browser control |

## Examples

| Example | Description |
|---------|-------------|
| [osc-control](examples/osc-control) | OSC protocol send/receive |
| [udp-receiver](examples/udp-receiver) | UDP input handling |
| [web-control](examples/web-control) | Browser-based control panel |

## Quick Start: OSC

```cpp
#include <vivid/vivid.h>
#include <vivid/network/network.h>

using namespace vivid::network;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Receive OSC on port 8000
    chain.add<OSCIn>("osc")
        .port(8000);

    // Map OSC to parameters
    chain.add<Noise>("noise")
        .scale([&]() { return osc.get("/scale", 1.0f); });

    chain.add<Output>("out")
        .input("noise");
}
```

## Quick Start: Web Control

```cpp
// Start web server on port 3000
chain.add<WebServer>("web")
    .port(3000);

// Access control panel at http://localhost:3000
// Sliders auto-generated from chain parameters
```

## OSC Addresses

When using OSC control, parameters are addressable as:
- `/operator_name/parameter_name` - Set parameter value
- `/operator_name/parameter_name?` - Query current value

## API Reference

See [LLM-REFERENCE.md](../../docs/LLM-REFERENCE.md) for complete operator documentation.

## Dependencies

- vivid-core
- oscpack (bundled for OSC)
- cpp-httplib (bundled for WebServer)

## License

MIT
