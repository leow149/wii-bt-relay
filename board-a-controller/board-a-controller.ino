/*
 * Board A — controller-facing firmware.
 *
 * Pairs to a real Bluetooth controller via Bluepad32 and forwards a compact
 * button/axis state struct to Board B over UART.
 *
 * NOTE ON API SURFACE: Bluepad32's Arduino API has shifted slightly across
 * versions (e.g. helper method names on the Controller object). This follows
 * the documented pattern as of Bluepad32's published examples, but check your
 * installed version's example sketches (Arduino IDE -> Examples -> Bluepad32)
 * before assuming this compiles unmodified — treat this as "should work,
 * verify against your installed library version," per docs/STATUS.md.
 *
 * Build: Arduino IDE or PlatformIO, ESP32 board package + Bluepad32 library
 * installed per https://bluepad32.readthedocs.io
 */
#include <Arduino.h>
#include <Bluepad32.h>
#include "shared_protocol.h"

#define UART_TX_PIN 17
#define UART_RX_PIN 16
HardwareSerial BoardBLink(1); /* UART1 */

ControllerPtr controllers[BP32_MAX_GAMEPADS];
static uint8_t g_seq = 0;

void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (controllers[i] == nullptr) {
            Serial.printf("Controller connected, slot %d\n", i);
            controllers[i] = ctl;
            return;
        }
    }
    Serial.println("No free slot for new controller");
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (controllers[i] == ctl) {
            Serial.printf("Controller disconnected, slot %d\n", i);
            controllers[i] = nullptr;
            return;
        }
    }
}

static uint16_t map_buttons(ControllerPtr ctl) {
    uint16_t out = 0;
    /* Bluepad32 exposes both a generic dpad() bitmask and named face-button
     * helpers depending on controller type. This is a conservative, generic
     * mapping — adjust to taste once you know which controller you're using.
     * See docs/PROTOCOL_NOTES.md for how these bits map onto Wiimote/Classic
     * Controller reports on Board B's side. */
    if (ctl->a())     out |= WII_BTN_A;
    if (ctl->b())     out |= WII_BTN_B;
    if (ctl->x())     out |= WII_BTN_ONE;
    if (ctl->y())     out |= WII_BTN_TWO;
    /* This Bluepad32 version (4.1.0, per the compile error that caught this)
     * doesn't have miscButtonSelect/Start/System() -- those were a guess at
     * an older/different API shape. The real per-button methods, confirmed
     * against Bluepad32's own keywords.txt, are miscBack()/miscSystem()/
     * miscHome(). The mapping below (back=minus, system=plus, home=home) is
     * a best-effort guess at which physical button each name corresponds to
     * across different controller brands (Xbox/PS/Switch all name their
     * "select" and "start" equivalents differently) -- verify against your
     * actual controller by watching Serial output once you can test, and
     * swap the mapping below if a button triggers the wrong action. */
    if (ctl->miscBack())   out |= WII_BTN_MINUS;
    if (ctl->miscSystem()) out |= WII_BTN_PLUS;
    if (ctl->miscHome())   out |= WII_BTN_HOME;

    uint8_t dpad = ctl->dpad();
    if (dpad & DPAD_UP)    out |= WII_BTN_UP;
    if (dpad & DPAD_DOWN)  out |= WII_BTN_DOWN;
    if (dpad & DPAD_LEFT)  out |= WII_BTN_LEFT;
    if (dpad & DPAD_RIGHT) out |= WII_BTN_RIGHT;

    return out;
}

void setup() {
    Serial.begin(115200);
    BoardBLink.begin(WII_RELAY_UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys(); /* comment out once you want persistent pairing */

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) controllers[i] = nullptr;

    Serial.println("Board A ready — put your controller in pairing mode");
}

void loop() {
    bool dataUpdated = BP32.update();
    if (!dataUpdated) {
        delay(1);
        return;
    }

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        ControllerPtr ctl = controllers[i];
        if (ctl == nullptr || !ctl->isConnected() || !ctl->hasData()) continue;
        if (ctl->isGamepad()) {
            wii_relay_frame_t frame;
            frame.frame_start = WII_RELAY_FRAME_START;
            frame.buttons = map_buttons(ctl);
            frame.stick_x = (int8_t)(ctl->axisX() >> 2);   /* Bluepad32 axes are ~10-bit signed; scale down to int8 */
            frame.stick_y = (int8_t)(ctl->axisY() >> 2);
            frame.seq = g_seq++;
            frame.checksum = wii_relay_checksum(&frame);

            BoardBLink.write((uint8_t *)&frame, sizeof(frame));
        }
    }
}
