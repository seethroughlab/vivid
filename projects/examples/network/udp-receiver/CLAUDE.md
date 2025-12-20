# UDP Receiver

Demonstrates receiving raw UDP packets and visualizing the data. Interprets packets as text, hex, and float arrays.

## Vision

A debug/visualization tool for incoming UDP data. Shows packet contents in multiple formats with animated visualization of float values as bar graphs. Useful for testing network integrations.

## Network Configuration

- **Listen Port**: 5000

## Data Interpretation

Incoming packets are displayed as:
- Hex dump (first 32 bytes)
- ASCII text (if printable)
- Float array (if divisible by 4 bytes)

## Visualization

- Packet counter
- Last message with fade animation
- Bar graph for float values (0-1 range)
- Connection status indicator (pulsing green when listening)

## Testing

```bash
# Send text
echo "Hello from UDP" | nc -u 127.0.0.1 5000

# Send binary floats (Python)
python3 -c "import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.sendto(bytes([0,0,128,63,0,0,0,64]), ('127.0.0.1', 5000))"
```
