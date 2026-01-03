# Web Control

HTTP server with REST API for remote parameter control via web browser.

## Vision

Control Vivid chains from any device with a web browser. The built-in web server serves a control interface and exposes operator parameters through a REST API. Perfect for tablet/phone remote control.

## Network Configuration

- **HTTP Port**: 8080
- **URL**: http://localhost:8080

## REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/operators` | GET | List all operators in chain |
| `/api/operator/:id` | GET | Get operator parameters |
| `/api/operator/:id` | POST | Set operator parameters |

## Visual Chain

Simple demo chain to control:
- Noise generator (scale, speed, octaves)
- HSV color adjustment (hue, saturation, value)
- Blur effect (radius, passes)

## Static Files

Web UI files served from `examples/network/web-control/web/`
