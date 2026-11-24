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

