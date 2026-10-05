# adf4350

Arduino (Digistump Oak, ESP8266) firmware to program an ADF4350 RF
synthesizer module over a serial-port menu. Settings persist in EEPROM.

## Features

- Set output frequency (Hz), 137.5 MHz – 4.4 GHz (VCO 2.2–4.4 GHz with
  2^d output dividers)
- Set RF output power level 0 (lowest) .. 3 (highest)
- Set reference frequency (Hz)
- Reference correction: enter the measured output from a frequency counter;
  the firmware computes a corrected effective reference so the next run
  produces the requested frequency
- Save software revision (major.minor), reference frequency, power level and
  output frequency to EEPROM, re-loaded on boot
- Header on every menu iteration: software revision, lock status, requested
  frequency, power level

## Hardware

- Digistump Oak (ESP8266), `esp8266:esp8266:oak` core
- ADF4350 module with on-board 25 MHz reference crystal
- Wiring:

  | ADF4350 pin | Oak pin | Note |
  |-------------|---------|------|
  | 1 PDR       | 3.3V    | power down released |
  | 2 LD        | P5      | lock detect input |
  | 3 MUX       | P10     | digital lock detect |
  | 4 CLK       | P9      | SCLK |
  | 5 DAT       | P7      | MOSI/SER |
  | 6 LE        | P6      | latch enable |
  | 7 GND       | GND     | |
  | 8 CE        | 3.3V    | **must be tied HIGH** |
  | 9 PDR       | 3.3V    | |
  | 10 GND      | GND     | |

- AD8307 RF power detector: AD8307 output -> 10k -> Oak P11 (the Oak's A0 ADC)
  with 18k from P11 to GND; firmware reports dBm on the Oak's 0–1 V ADC
  (`readPowerDbm()`), shown in the status header and on menu `m`
- FTDI USB-serial to Oak TX/RX at 3.3V levels
- To enter firmware upload mode: jumper P2/SCL to GND and power-cycle,
  then remove the jumper and reset for normal boot

## Build / flash / monitor

```bash
cd adf4350
make compile   # arduino-cli compile (recompiles only when adf4350.ino changed)
make upload    # compile if needed, then upload via esptool
make monitor   # serial monitor at 115200 baud
make clean     # force full recompile next time
```

Override port/board as needed:

```bash
make upload PORT=/dev/tty.usbserial-4
```

## Using the menu

Serial at 115200 8N1. Each displayed header shows the revision, lock status,
requested frequency, power level, and effective reference. Choose:

f) Frequency in MHz, 1 Hz resolution (e.g. `1000.000000`)
p) Power level 0–3
r) New (nominal) reference frequency in MHz
t) Settings: frequency offset — enter measured output in MHz to apply an
   offset to the estimated output, or 0 to reset it to 0; b to go back
s) Save current settings to EEPROM (also auto-saved on frequency/power change)
v) Redraw status header
d) Device select: ADF4350 or ADF4351
m) Print measured RF power (AD8307 on P11)
