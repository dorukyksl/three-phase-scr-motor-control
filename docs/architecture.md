# Controller notes

The main sketch is `motor_test_ble.ino`. It runs on an Arduino Uno with an ATmega328P at 16 MHz and uses AVR registers directly.

## Phase timing

The three phase inputs produce six valid sector codes. In order, these are `010, 011, 001, 101, 100, 110`. The firmware maps them to T1 R+, T2 T-, T3 S+, T4 R-, T5 T+ and T6 S-.

PCINT1 handles the phase transitions. It checks their order and spacing, estimates the period and compensates for the phase detector's dead band. Timer1 runs every 50 µs and switches the gate outputs according to the scheduled windows. Command parsing, divisions, ADC processing and telemetry stay in the main loop.

The firing delay includes the phase reference, dead-band correction, 30-degree natural commutation offset and the requested angle. Gate pulses are 70 degrees wide. The angle can be set between 20 and 150 degrees, and the active value moves toward the target by one degree every 200 ms.

## Connections

```text
A1, A2, A3   phase inputs R, S, T
A0           tachogenerator ADC
D5 to D10    gate outputs T1 to T6
D0, D1       HM-10 serial connection, 9600 baud
FFE0         BLE service
FFE1         BLE characteristic
```

These are the logical assignments in the code. Check them against the actual circuit before making connections.

We used the hardware UART because SoftwareSerial interrupts the timing needed by the firing controller. It shares D0/D1 with USB serial, so the HM-10 TX connection needs to be disconnected when uploading through USB.

## Speed measurement

The Arduino averages eight ADC samples and applies an exponential filter. Readings below 30 RPM are treated as zero. The calibration in this version uses 1032 RPM/V and an ADC reference of 4.862 V, both taken from the original setup.

Speed is measured for display. The firmware does not adjust the firing angle automatically to maintain a requested RPM.

## App

`BLEManager.swift` handles discovery, connection, commands and notifications. `Telemetry.swift` parses the incoming values, and `ContentView.swift` contains the controls and status display.

BLE notifications can split a telemetry line into several packets. The app collects bytes until LF arrives, then parses the complete line. Using a byte buffer also avoids treating Arduino's CRLF ending as a single Swift Character.

The app sends a heartbeat once a second. The firmware timeout is three seconds, but the watchdog is only enabled after the first heartbeat. The remaining details are in the [protocol](protocol.md) and [test notes](validation.md).

The [Turkish diagrams](diagrams) are from the project documentation. Some show earlier revisions.
