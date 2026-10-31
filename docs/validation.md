# Test notes

We checked the individual channels first, then the full bridge with a resistive load, and finally the motor. The [demo video](../media/lab-demo.mp4) shows the bench setup during testing.

## Motor measurements

These are the speed readings from the project notes:

```text
Firing angle     Recorded speed
105 degrees       190 RPM
 90 degrees       636 RPM
 75 degrees      1552 RPM
 60 degrees      2480 RPM
 45 degrees      3294 RPM
```

The corresponding voltage values used in the notes were 3.7, 14.7, 32.2, 55.0 and 77.8 V. Those are theoretical values for the resistive-load model, not separate measurements of the motor voltage.

The tachogenerator calibration gave approximately 1032 RPM/V. Correcting the ADC reference from the nominal 5 V to 4.862 V addressed the scale error in the original setup. That correction depends on the board and its supply; it needs to be checked again if either changes.

The screenshots were captured during the project and contain readings from a different stage of calibration. Raw acquisition logs and an uncertainty analysis are not included here.

## Build checks

All twelve Arduino sketches have compiled successfully for the Uno with Arduino CLI 1.5.1 and AVR core 1.8.8 on Linux. The [GitHub Actions workflow](../.github/workflows/firmware.yml) repeats that check.

The iOS app also built for the simulator with SDK 27.1 and code signing disabled. These are build checks only; they do not test the connected controller or motor.

## Things to fix

The controller is open loop, and `RPM_TRIP` is currently set to zero, which disables the overspeed trip. The enable check should account for the active angle as well as the target, since the ramp may still be in progress.

The heartbeat watchdog becomes active only after receiving `H` and deactivates itself after a timeout. Phase loss clears synchronization and scheduled pulses without necessarily clearing the firing request. That means firing can resume when synchronization returns. Both behaviors need review before further powered use.

The command parser needs an explicit reject state for oversized lines. It currently drops excess bytes. There is also a small display inconsistency: the firmware calculates RPM with a 4.862 V reference, while the app's `tachoVolts` helper still uses 5 V.

The voltage shown in the app is calculated rather than measured. Burst scaling is a model estimate too. Finally, the July hardware exports need the later bench changes carried into them before they can represent the working circuit.
