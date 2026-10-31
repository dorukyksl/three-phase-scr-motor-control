# Test sketches

These are the smaller programs and earlier controller versions from the bench work.

We started with `test` and `opto_test` for the logic and optocoupler channels. `LTV_ZCD_Test` and `LTV_ZCD_Test_47V` were used to check the phase-detection wiring and timing.

`gate_test`, `tek_scr_test`, `tekfaztest` and `tekfaztest_allchannel` cover the gate-driver and individual SCR tests. `tum_faz_res_test` is the full-bridge resistive-load test.

`motor_test` is an earlier motor controller. `motor_test_pot` adds potentiometer control. The later BLE version is in [firmware/main/motor_test_ble](../main/motor_test_ble).

Several of these sketches drive gate outputs directly and assume a particular test setup. Do not upload them to an energized assembly.
