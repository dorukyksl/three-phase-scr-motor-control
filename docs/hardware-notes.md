# Hardware notes

The files in [archive-2026-07-29](../hardware/archive-2026-07-29) are the schematic, PCB layout, BOM and netlist exported on 29 July 2026. The PDFs were exported from EasyEDA. I also added a [UTF-8 copy of the BOM](../hardware/archive-2026-07-29/bom-utf8.csv) so it can be read on GitHub.

The working setup in the video was assembled on perfboard and breadboard. It changed during testing, and the July drawings were not updated to include every change. There are no Gerber files or editable board files in this repository.

## Changes made after the drawings

The project notes call for series diodes in the phase-detection inputs and blocking diodes in each gate path. They also record changes to the gate-series and optocoupler LED resistors.

Other items still need to be carried into the PCB revision: resistor power ratings, snubber selection, supply decoupling, connector grounds, discharge provision and test points.

The notes say that branch fuses were not fitted to the physical prototype at that stage, even though the drawings include fuse footprints. The exports therefore do not describe a finished board ready to manufacture.

We tested at the 47 V phase setting. The higher 94 V setting was left for later work.

## Before using the circuit

The isolated supply still produces hazardous voltages. Stored energy and the rotating motor also need proper protection. The optocouplers' component ratings do not establish an isolation rating for the complete assembly.

The drawings and firmware need an electrical safety review before powered use. Burst firing is included for resistive loads and should not be used as a motor-control mode.
