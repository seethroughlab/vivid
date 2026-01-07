# Arduino LED Control

Demonstrates serial communication with Arduino for LED brightness control.

## Operators Used

- **SerialOut** - Send data to serial port
- **LFO** - Generate oscillating values

## Key Concepts

### Serial Output
```cpp
auto& serial = chain.add<SerialOut>("arduino");

// Set port (platform-specific)
// macOS: /dev/tty.usbmodem* or /dev/cu.usbserial-*
// Linux: /dev/ttyUSB0 or /dev/ttyACM0
// Windows: COM3, COM4, etc.
serial.port("/dev/tty.usbmodem14201");
serial.baudRate = 9600;

// Send data
serial.sendInt(brightness);      // Integer
serial.sendFloat(value);         // Float
serial.sendByte(byteValue);      // Single byte
serial.sendString("hello");      // String
serial.sendBytes(buffer, len);   // Raw bytes
```

### LFO to LED Brightness
```cpp
// In setup:
auto& lfo = ctx.chain().add<LFO>("pulse");
lfo.frequency = 0.5f;  // 0.5 Hz = 2 second cycle

// In update:
float value = ctx.chain().get<LFO>("pulse").value();  // 0 to 1
int brightness = static_cast<int>(value * 255);        // 0 to 255
serial.sendInt(brightness);
```

## Arduino Sketch

```cpp
void setup() {
    Serial.begin(9600);
    pinMode(9, OUTPUT);  // PWM pin
}

void loop() {
    if (Serial.available()) {
        int brightness = Serial.parseInt();
        analogWrite(9, brightness);
    }
}
```

## Hardware Setup

1. Connect Arduino via USB
2. Connect LED with resistor to PWM pin 9
3. Find serial port name (check Arduino IDE)
4. Update port path in chain.cpp
5. Upload Arduino sketch first, then run Vivid

## Common Serial Ports

| Platform | Port Format |
|----------|-------------|
| macOS | `/dev/tty.usbmodem*` or `/dev/cu.usbserial-*` |
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| Windows | `COM3`, `COM4`, etc. |

## Controls

No interactive controls - LED pulses automatically based on LFO.
