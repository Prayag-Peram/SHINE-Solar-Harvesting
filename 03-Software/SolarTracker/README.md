# SHINE Python Solar Tracker — Simulation Prototype

Laptop-side simulation of hybrid astronomical solar tracking with weather-aware calibration. **No motors are commanded.** The simulated photodiode is not a validated optical model.

## Install

```bash
python -m pip install -r requirements.txt
```

## Run

```bash
python tracker.py --lat 30.62 --lon -96.34 --once
python tracker.py --lat 30.62 --lon -96.34 --interval 30
```

Replace example coordinates with the installation site. Internet is needed for weather; solar position is calculated locally. The program works in predictive-only mode if weather is unavailable.

## Code layout
- `solar_position`: pvlib azimuth/elevation from UTC and location
- `WeatherClient`: Open-Meteo DNI, cloud cover, wind (cached)
- `SimulatedPhotodiode`: mock light measurement, **not** real hardware
- `Tracker.scan`: bounded virtual search, **no actuation**
- `Tracker.step`: modes and target angles, **no USB commands**

## Before physical use
1. Confirm one or two ESP32 boards and serial port assignments.
2. Confirm both axis encoders, absolute homing references, gear ratios and travel/cable limits.
3. Identify photodiode amplifier/ADC, channel count, saturation and sample timing.
4. Implement hardware sensor reads, motion completion acknowledgements and bounded search as a separate integration layer. A single unshaded photodiode requires a real scan; a quadrant sensor can use differential error signals if properly optically configured.
5. Add independent emergency stop, beam shutter/cover, watchdog, overtemperature and wind interlocks. Never run unverified software with an exposed concentrating lens.
6. Replace simulated 'ideal' light normalization with experimentally established reference measurements; forecast DNI is not a direct local measurement.
7. Verify weather API field availability, licensing and timestamp freshness for the deployment.

This is a development prototype, not production or safety-certified tracking software.
