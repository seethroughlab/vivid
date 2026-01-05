// Arduino LED Control Example
//
// This example sends an LFO value to an Arduino to control LED brightness.
//
// Arduino sketch:
//   void setup() { Serial.begin(9600); pinMode(9, OUTPUT); }
//   void loop() {
//       if (Serial.available()) {
//           int brightness = Serial.parseInt();
//           analogWrite(9, brightness);
//       }
//   }

#include <vivid/vivid.h>
#include <vivid/serial/serial_out.h>

using namespace vivid;
using namespace vivid::serial;

void setup(Context& ctx) {
    // LFO generates 0-1 sine wave
    auto& lfo = ctx.chain().add<LFO>("pulse");
    lfo.frequency = 0.5f;

    // Serial output to Arduino
    auto& serial = ctx.chain().add<SerialOut>("arduino");

    // Change this to your Arduino's serial port:
    // macOS: /dev/tty.usbmodem14201 or /dev/cu.usbserial-*
    // Linux: /dev/ttyUSB0 or /dev/ttyACM0
    // Windows: COM3, COM4, etc.
    serial.port("/dev/tty.usbmodem14201");
    serial.baudRate = 9600;
}

void update(Context& ctx) {
    ctx.chain().process();

    // Get LFO value and send to Arduino as 0-255
    auto& serial = ctx.chain().get<SerialOut>("arduino");
    float value = ctx.chain().get<LFO>("pulse").value();

    // Map 0-1 to 0-255 for Arduino analogWrite
    int brightness = static_cast<int>(value * 255);
    serial.sendInt(brightness);
}

VIVID_CHAIN(setup, update)
