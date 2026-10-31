# BLE and serial protocol

The Arduino and HM-10 communicate at 9600 baud. The app uses BLE service `FFE0` and characteristic `FFE1`.

## Commands

Each command is an ASCII line ending in LF or CRLF. Uppercase and lowercase letters are accepted.

```text
A90   set the target angle to 90 degrees
90    same as A90
E     enable firing if the target angle is at least 140 degrees
D     disable firing
H     start or refresh the heartbeat watchdog
?     send telemetry now
B50   select burst mode at 50% duty, for resistive loads
F     return to phase control
```

The angle range is 20 to 150 degrees. Burst duty is 0 to 100%.

The enable check only looks at the target angle. It does not check whether the active angle has finished ramping. The watchdog also needs an `H` command before it becomes active. If it times out while firing, it disables firing and deactivates itself until another heartbeat arrives.

## Telemetry

The Arduino sends a line every 200 ms:

```text
S,alpha,targetAlpha,vdcTenths,frequencyHundredths,synced,firing,sector,noiseCount,faultCount,badCommandCount,mode,duty,rpm,tachoAdc
```

For example, this illustrates the format rather than a saved measurement:

```text
S,90,90,147,5000,1,1,0,0,0,0,0,0,636,130
```

`alpha` and `targetAlpha` are angles in degrees. Divide `vdcTenths` by 10 for the theoretical resistive-load voltage and `frequencyHundredths` by 100 for Hz. Frequency is reported as zero while the controller is not synchronized.

`synced` and `firing` are 0/1 flags. `sector` is 0 to 5, or -1 when unknown. The next three fields count ignored phase events, synchronization faults and unrecognized commands.

`mode` is 0 for phase control and 1 for burst control. `duty` is the burst percentage. `rpm` is the calculated tachogenerator speed, and `tachoAdc` is the filtered ADC reading.

Lines starting with `#` are diagnostic messages. The app handles them separately and accepts older telemetry lines without the optional fields.

The fixed command buffer still needs better overflow handling. See the [test notes](validation.md#things-to-fix).
