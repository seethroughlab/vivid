# Vivid WebSocket API

The WebSocket API allows external tools to control a running Vivid app in real-time. This is the same API used by the VSCode extension for live parameter editing, hot-reload, and performance monitoring.

## Quick Start

Connect to a running Vivid app and change a parameter:

```python
import asyncio
import websockets
import json

async def main():
    async with websockets.connect("ws://localhost:9876") as ws:
        # Request current state
        await ws.send(json.dumps({"type": "request_operators"}))

        # Receive operator list, param values, and window state
        for _ in range(3):
            msg = json.loads(await ws.recv())
            print(f"Received: {msg['type']}")

        # Change a parameter
        await ws.send(json.dumps({
            "type": "param_change",
            "operator": "noise",
            "param": "scale",
            "value": [8.0, 0.0, 0.0, 0.0]
        }))

asyncio.run(main())
```

## Connection Details

| Property | Value |
|----------|-------|
| Port | 9876 |
| Address | 0.0.0.0 (all interfaces) |
| Protocol | WebSocket |
| Message Format | JSON |

The server starts automatically when the Vivid runtime launches.

---

## Commands (Client → Runtime)

### reload

Triggers a hot-reload of the chain.cpp file.

```json
{"type": "reload"}
```

**Response**: `compile_status` message with success/failure.

---

### param_change

Changes a parameter value on an operator.

```json
{
  "type": "param_change",
  "operator": "noise",
  "param": "scale",
  "value": [4.0, 0.0, 0.0, 0.0]
}
```

| Field | Type | Description |
|-------|------|-------------|
| operator | string | Chain name of the operator |
| param | string | Parameter name |
| value | float[4] | Parameter value (up to 4 components for Vec4/Color) |

---

### solo_node

Isolates a single operator's output for preview.

```json
{
  "type": "solo_node",
  "operator": "blur"
}
```

**Response**: `solo_state` message broadcast to all clients.

---

### solo_exit

Returns to normal chain output.

```json
{"type": "solo_exit"}
```

**Response**: `solo_state` message broadcast to all clients.

---

### select_node

Highlights a node in the chain graph visualization.

```json
{
  "type": "select_node",
  "operator": "noise"
}
```

---

### focused_node

Sets or clears focus on a node (3x larger preview in visualizer).

```json
{
  "type": "focused_node",
  "operator": "blur"
}
```

Clear focus with empty string:

```json
{
  "type": "focused_node",
  "operator": ""
}
```

---

### request_operators

Requests the current chain state.

```json
{"type": "request_operators"}
```

**Response**: Three messages are sent:
1. `operator_list` - Chain structure
2. `param_values` - Current parameter values
3. `window_state` - Window configuration

---

### window_control

Controls window properties.

**Toggle fullscreen:**
```json
{
  "type": "window_control",
  "setting": "fullscreen",
  "value": 1
}
```

**Switch monitor:**
```json
{
  "type": "window_control",
  "setting": "monitor",
  "value": 1
}
```

| Setting | Value | Description |
|---------|-------|-------------|
| fullscreen | 0/1 | Toggle fullscreen mode |
| borderless | 0/1 | Toggle borderless window |
| alwaysOnTop | 0/1 | Toggle always-on-top |
| cursorVisible | 0/1 | Toggle cursor visibility |
| monitor | index | Switch to monitor by index |

**Response**: `window_state` message.

---

### request_window_state

Requests current window configuration.

```json
{"type": "request_window_state"}
```

**Response**: `window_state` message.

---

### request_pending_changes

Requests current pending parameter changes (Claude-first workflow).

```json
{"type": "request_pending_changes"}
```

**Response**: `pending_changes` message.

---

### commit_changes

Clears the pending changes queue (call after Claude applies changes to chain.cpp).

```json
{"type": "commit_changes"}
```

**Response**: `pending_changes` message with empty changes array.

---

### discard_changes

Discards pending changes and reverts parameters to their original values.

```json
{"type": "discard_changes"}
```

**Response**: `pending_changes` message with empty changes array. Runtime reverts parameter values.

---

## Messages (Runtime → Client)

### compile_status

Sent after hot-reload completes.

```json
{
  "type": "compile_status",
  "success": true,
  "message": ""
}
```

On failure, `message` contains the compiler error with file:line:column format.

---

### operator_list

Chain structure with source line mapping.

```json
{
  "type": "operator_list",
  "operators": [
    {
      "name": "noise",
      "displayName": "Noise",
      "outputType": "Texture",
      "sourceLine": 5,
      "inputs": []
    },
    {
      "name": "blur",
      "displayName": "Blur",
      "outputType": "Texture",
      "sourceLine": 6,
      "inputs": ["noise"]
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| name | Chain name (variable name) |
| displayName | Operator type (e.g., "Noise", "Blur") |
| outputType | Output kind: "Texture", "Audio", or "3D" |
| sourceLine | Line number in chain.cpp |
| inputs | Array of connected input operator names |

---

### param_values

Current parameter values for all operators.

```json
{
  "type": "param_values",
  "params": [
    {
      "operator": "noise",
      "name": "scale",
      "type": "Float",
      "value": [4.0, 0.0, 0.0, 0.0],
      "min": 0.0,
      "max": 10.0
    },
    {
      "operator": "colorize",
      "name": "color",
      "type": "Color",
      "value": [1.0, 0.0, 0.0, 1.0],
      "min": 0.0,
      "max": 1.0
    }
  ]
}
```

**Parameter Types:**
- `Float` - Single float (value[0])
- `Int` - Integer (value[0])
- `Bool` - Boolean (value[0])
- `Vec2` - 2D vector (value[0], value[1])
- `Vec3` - 3D vector (value[0], value[1], value[2])
- `Vec4` - 4D vector (all 4 components)
- `Color` - RGBA color (all 4 components)
- `String` - String value (uses `stringValue` field)
- `FilePath` - File path (uses `stringValue`, optional `fileFilter` and `fileCategory`)

---

### performance_stats

Sent periodically with performance metrics.

```json
{
  "type": "performance_stats",
  "fps": 60.0,
  "frameTimeMs": 16.67,
  "fpsHistory": [59.8, 60.1, 59.9],
  "frameTimeHistory": [16.71, 16.63, 16.68],
  "textureMemoryBytes": 33554432,
  "operatorCount": 5,
  "operatorTimings": [
    {"name": "noise", "timeMs": 2.5},
    {"name": "blur", "timeMs": 4.2}
  ]
}
```

| Field | Description |
|-------|-------------|
| fps | Current frames per second |
| frameTimeMs | Last frame time in milliseconds |
| fpsHistory | Recent FPS values (last 60 samples) |
| frameTimeHistory | Recent frame times (last 60 samples) |
| textureMemoryBytes | Estimated GPU texture memory |
| operatorCount | Number of operators in chain |
| operatorTimings | Per-operator processing time |

---

### solo_state

Solo mode status.

```json
{
  "type": "solo_state",
  "active": true,
  "operator": "blur"
}
```

When inactive, `operator` field is omitted.

---

### window_state

Window configuration and available monitors.

```json
{
  "type": "window_state",
  "fullscreen": false,
  "borderless": false,
  "alwaysOnTop": false,
  "cursorVisible": true,
  "currentMonitor": 0,
  "monitors": [
    {
      "index": 0,
      "name": "Built-in Retina Display",
      "width": 2560,
      "height": 1600
    },
    {
      "index": 1,
      "name": "LG UltraWide",
      "width": 3440,
      "height": 1440
    }
  ]
}
```

---

### pending_changes

Pending parameter changes from slider adjustments (Claude-first workflow).

Broadcast when:
- A parameter is changed via `param_change` command
- `commit_changes` or `discard_changes` is called
- `request_pending_changes` is received

```json
{
  "type": "pending_changes",
  "hasChanges": true,
  "changes": [
    {
      "operator": "noise",
      "param": "scale",
      "paramType": "Float",
      "oldValue": [4.0, 0.0, 0.0, 0.0],
      "newValue": [8.0, 0.0, 0.0, 0.0],
      "sourceLine": 12,
      "timestamp": 1735123456789
    }
  ]
}
```

| Field | Description |
|-------|-------------|
| hasChanges | Whether there are pending changes |
| changes | Array of pending parameter changes |
| operator | Chain name of the operator |
| param | Parameter name |
| paramType | Type: Float, Int, Bool, Vec2, Vec3, Vec4, Color |
| oldValue | Original value before slider adjustment |
| newValue | New value from slider |
| sourceLine | Line number in chain.cpp |
| timestamp | Unix timestamp in milliseconds |

**Claude-First Workflow:**
1. User adjusts slider → `param_change` is applied immediately (preview visible)
2. Runtime stores change in pending queue and broadcasts `pending_changes`
3. Claude calls MCP tool `get_pending_changes` to see changes
4. Claude edits chain.cpp with new values
5. Claude calls `commit_changes` → queue cleared, `pending_changes` broadcast with empty array

---

## Usage Examples

### Python: Live Parameter Animation

```python
import asyncio
import websockets
import json
import math

async def animate_param():
    async with websockets.connect("ws://localhost:9876") as ws:
        t = 0
        while True:
            # Animate scale with sine wave
            scale = 2.0 + math.sin(t) * 2.0
            await ws.send(json.dumps({
                "type": "param_change",
                "operator": "noise",
                "param": "scale",
                "value": [scale, 0, 0, 0]
            }))
            t += 0.1
            await asyncio.sleep(0.016)  # ~60fps

asyncio.run(animate_param())
```

### Node.js: Performance Monitor

```javascript
const WebSocket = require('ws');

const ws = new WebSocket('ws://localhost:9876');

ws.on('open', () => {
    ws.send(JSON.stringify({ type: 'request_operators' }));
});

ws.on('message', (data) => {
    const msg = JSON.parse(data);

    if (msg.type === 'performance_stats') {
        console.log(`FPS: ${msg.fps.toFixed(1)}`);
        console.log(`Frame time: ${msg.frameTimeMs.toFixed(2)}ms`);

        if (msg.operatorTimings) {
            console.log('Operator timings:');
            msg.operatorTimings.forEach(op => {
                console.log(`  ${op.name}: ${op.timeMs.toFixed(2)}ms`);
            });
        }
    }
});
```

### Python: Hot-Reload Watcher

```python
import asyncio
import websockets
import json
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler

class ReloadHandler(FileSystemEventHandler):
    def __init__(self, ws):
        self.ws = ws

    def on_modified(self, event):
        if event.src_path.endswith('chain.cpp'):
            asyncio.run(self.ws.send(json.dumps({"type": "reload"})))

async def watch_and_reload(project_path):
    async with websockets.connect("ws://localhost:9876") as ws:
        # Listen for compile results
        async def listen():
            async for message in ws:
                msg = json.loads(message)
                if msg['type'] == 'compile_status':
                    if msg['success']:
                        print("Reload successful")
                    else:
                        print(f"Compile error: {msg['message']}")

        # Set up file watcher
        handler = ReloadHandler(ws)
        observer = Observer()
        observer.schedule(handler, project_path, recursive=False)
        observer.start()

        await listen()
```

---

## Building Custom Controllers

The WebSocket API enables building custom control interfaces:

1. **MIDI Controllers** - Map MIDI CC values to `param_change` commands
2. **OSC Bridges** - Convert OSC messages to WebSocket commands
3. **Web Dashboards** - Browser-based control panels
4. **Mobile Apps** - Touch interfaces for live performance
5. **Automation Scripts** - Scripted parameter sequences

### Connection Flow

1. Connect to `ws://localhost:9876`
2. Send `request_operators` to get initial state
3. Receive `operator_list`, `param_values`, `window_state`
4. Send commands as needed
5. Listen for `performance_stats` and `compile_status` updates

### Best Practices

- **Throttle updates** - Don't send faster than 60fps
- **Handle disconnects** - Reconnect if the runtime restarts
- **Validate operator names** - Check against `operator_list` before sending
- **Use parameter ranges** - Respect `min`/`max` from `param_values`

---

## See Also

- [CHAIN-API.md](CHAIN-API.md) - Chain programming API
- [OPERATOR-API.md](OPERATOR-API.md) - Creating custom operators
- `core/include/vivid/editor_bridge.h` - API header
- `core/src/editor_bridge.cpp` - Implementation
