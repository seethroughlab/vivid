# WebSocket UI

Browser-based control interface using WebServer with WebSocket real-time updates.

## Operators Used

- **WebServer** - HTTP server with WebSocket support
- **Noise** - Procedural noise generator
- **HSV** - Color adjustment
- **Transform** - Rotation control

## Features

- REST API for parameter control
- WebSocket for real-time bidirectional updates
- Static file serving for web UI
- Custom route handlers

## Running

1. Start the example: `./build/bin/vivid modules/vivid-network/examples/websocket-ui`
2. Open http://localhost:8080 in your browser
3. Use the sliders to control the visualization

## Key Concepts

### WebServer Setup
```cpp
auto& web = chain.add<WebServer>("web");
web.port(8080);
web.staticDir("path/to/web/");  // Serve HTML/CSS/JS
```

### Custom Routes
```cpp
// GET/POST handler
web.route("/api/state", [](const std::string& method,
                           const std::string& path,
                           const std::string& body) -> std::string {
    if (method == "GET") {
        return "{\"value\": 42}";
    } else {
        // Parse body, update state
        return "{\"status\": \"ok\"}";
    }
});
```

### WebSocket Broadcast
```cpp
// Send to all connected clients
web.broadcast("{\"type\":\"update\",\"value\":42}");

// Send typed JSON
web.broadcastJson("state", "{\"hue\":0.5}");
```

### Checking Connections
```cpp
if (web.isRunning()) {
    size_t clients = web.connectionCount();
    // ...
}
```

## Built-in API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/operators` | GET | List all chain operators |
| `/api/operator/:id` | GET | Get operator parameters |
| `/api/operator/:id` | POST | Set operator parameters |
| `/ws` | WebSocket | Real-time bidirectional |

## Web UI Files

Create `web/index.html`:
```html
<!DOCTYPE html>
<html>
<head>
    <title>Vivid Control</title>
    <style>
        body { font-family: sans-serif; padding: 20px; }
        .slider-group { margin: 10px 0; }
        label { display: inline-block; width: 100px; }
        input[type="range"] { width: 300px; }
    </style>
</head>
<body>
    <h1>Vivid WebSocket Control</h1>

    <div class="slider-group">
        <label>Hue:</label>
        <input type="range" id="hue" min="0" max="1" step="0.01" value="0">
        <span id="hue-value">0</span>
    </div>

    <div class="slider-group">
        <label>Scale:</label>
        <input type="range" id="scale" min="1" max="10" step="0.1" value="4">
        <span id="scale-value">4</span>
    </div>

    <div class="slider-group">
        <label>Speed:</label>
        <input type="range" id="speed" min="0" max="2" step="0.1" value="0.5">
        <span id="speed-value">0.5</span>
    </div>

    <div class="slider-group">
        <label>Animate:</label>
        <input type="checkbox" id="animate" checked>
    </div>

    <p>Connected: <span id="status">No</span></p>

    <script>
        // Connect to WebSocket
        const ws = new WebSocket(`ws://${window.location.host}/ws`);

        ws.onopen = () => {
            document.getElementById('status').textContent = 'Yes';
        };

        ws.onclose = () => {
            document.getElementById('status').textContent = 'No';
        };

        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            // Update UI from server state
            if (data.type === 'state') {
                // Sync sliders with server
            }
        };

        // Send updates on slider change
        function sendUpdate(param, value) {
            fetch('/api/update', {
                method: 'POST',
                body: `${param}=${value}`
            });
        }

        document.getElementById('hue').addEventListener('input', (e) => {
            document.getElementById('hue-value').textContent = e.target.value;
            sendUpdate('hue', e.target.value);
        });

        document.getElementById('scale').addEventListener('input', (e) => {
            document.getElementById('scale-value').textContent = e.target.value;
            sendUpdate('scale', e.target.value);
        });

        document.getElementById('speed').addEventListener('input', (e) => {
            document.getElementById('speed-value').textContent = e.target.value;
            sendUpdate('speed', e.target.value);
        });

        document.getElementById('animate').addEventListener('change', (e) => {
            sendUpdate('animate', e.target.checked);
        });
    </script>
</body>
</html>
```

## Use Cases

### Remote Control
Control installations from a phone or tablet on the same network.

### Multi-Display Sync
Broadcast state to multiple displays running the same chain.

### Data Logging
Receive parameter changes via WebSocket and log to file.

### External Integration
Connect to other software (Max/MSP, Processing, etc.) via WebSocket.

## Tips

1. Use low broadcast rates (10Hz) to avoid flooding
2. Debounce slider inputs on the client side
3. Use JSON for structured messages
4. Handle reconnection in the web client
5. Consider authentication for public networks
