"""SHINE hybrid sun tracker. SIMULATION ONLY: never commands motors.

Requires: pip install pvlib pandas requests
Example: python shine_tracker.py --lat 30.62 --lon -96.34 --once
"""
import argparse
import math
import time
from dataclasses import dataclass
from datetime import datetime, timezone

import pandas as pd
import pvlib
import requests


@dataclass
class Config:
    lat: float
    lon: float
    interval: float = 30.0
    weather_refresh: float = 900.0
    minimum_elevation: float = 10.0
    minimum_dni: float = 350.0
    maximum_cloud: float = 60.0
    minimum_total: float = 0.1
    saturation: float = 4090.0  # TODO: actual ADC full-scale counts
    error_deadband: float = 0.025
    max_offset: float = 3.0
    gain_deg_per_error: float = 0.3  # SIMULATION ONLY; calibrate optically
    correction_interval: float = 30.0
    recalibration_delay: float = 120.0
    recalibration_cooldown: float = 300.0
    filter_alpha: float = 0.3


def solar_position(cfg, now):
    row = pvlib.solarposition.get_solarposition(
        pd.DatetimeIndex([pd.Timestamp(now)]), cfg.lat, cfg.lon
    ).iloc[0]
    return float(row['azimuth']), float(row['apparent_elevation'])


class Weather:
    """Weather forecast is context, not a local measurement of direct sun."""
    def __init__(self, cfg):
        self.cfg = cfg
        self.last_request = -1e20
        self.data = None

    def get(self):
        t = time.monotonic()
        if t - self.last_request < self.cfg.weather_refresh:
            return self.data
        self.last_request = t
        try:
            r = requests.get('https://api.open-meteo.com/v1/forecast', params={
                'latitude': self.cfg.lat, 'longitude': self.cfg.lon,
                'current': 'cloud_cover,direct_normal_irradiance,wind_speed_10m',
                'timezone': 'UTC'
            }, timeout=8)
            r.raise_for_status()
            current = r.json()['current']
            stamp = datetime.fromisoformat(current['time']).replace(tzinfo=timezone.utc)
            if abs((datetime.now(timezone.utc) - stamp).total_seconds()) > 7200:
                raise ValueError('stale weather data')
            self.data = {
                'dni': float(current['direct_normal_irradiance']),
                'cloud': float(current['cloud_cover']),
                'wind': float(current['wind_speed_10m'])
            }
        except (requests.RequestException, KeyError, TypeError, ValueError) as exc:
            print('[weather] unavailable:', exc)
            self.data = None
        return self.data


def quadrant_error(channels, cfg):
    """Q1 upper-left, Q2 upper-right, Q3 lower-left, Q4 lower-right.

    channels must already be dark-offset corrected and gain matched.
    Positive x means more illumination on right, positive y on top.
    Motor sign and optical geometry MUST be determined experimentally.
    """
    if len(channels) != 4 or any(not math.isfinite(v) or v < 0 for v in channels):
        return None
    if any(v >= cfg.saturation for v in channels):
        return None
    q1, q2, q3, q4 = channels
    total = sum(channels)
    if total < cfg.minimum_total:
        return None
    x = ((q2 + q4) - (q1 + q3)) / total
    y = ((q1 + q2) - (q3 + q4)) / total
    return x, y, total


class SimulatedQuadrants:
    """Fake sensor for algorithm development; does not model real optics."""
    def __init__(self, az_bias=1.0, el_bias=-0.6):
        self.az_bias = az_bias
        self.el_bias = el_bias

    def read(self, target_az, target_el, sun_az, sun_el, weather):
        # Actual optical alignment occurs when command includes these biases.
        az_miss = ((sun_az + self.az_bias - target_az + 180) % 360) - 180
        el_miss = sun_el + self.el_bias - target_el
        # Arbitrary idealized sensor response; positive means sun is right/up.
        x = max(-0.95, min(0.95, az_miss / 3.0))
        y = max(-0.95, min(0.95, el_miss / 3.0))
        cloud_factor = 1.0 if weather is None else max(0.02, 1 - weather['cloud'] / 105)
        intensity = 1000.0 * cloud_factor
        return (
            intensity * (1-x) * (1+y) / 4,
            intensity * (1+x) * (1+y) / 4,
            intensity * (1-x) * (1-y) / 4,
            intensity * (1+x) * (1-y) / 4,
        )


class Tracker:
    def __init__(self, cfg):
        self.cfg = cfg
        self.weather = Weather(cfg)
        self.sensor = SimulatedQuadrants()
        self.az_offset = 0.0
        self.el_offset = 0.0
        self.filtered = None
        self.low_since = None
        self.last_correction = -1e20
        self.last_recalibration = -1e20
        self.mode = 'STARTUP'

    def read_quadrants(self, az, el, sun_az, sun_el, weather):
        # TODO REAL HARDWARE: obtain four ADC readings over USB from ESP32.
        # Convert ADC to dark-offset-corrected, gain-matched nonnegative values.
        # Check sensor orientation and saturation before enabling correction.
        return self.sensor.read(az, el, sun_az, sun_el, weather)

    def step(self):
        now = datetime.now(timezone.utc)
        t = time.monotonic()
        sun_az, sun_el = solar_position(self.cfg, now)
        weather = self.weather.get()
        if sun_el < self.cfg.minimum_elevation:
            self.mode = 'NIGHT_OR_LOW_SUN'
            self.filtered = None
            self.low_since = None
            return sun_az, sun_el, None, weather

        # Forecast-based gate: don't chase diffuse clouds or unknown weather.
        good_weather = (weather is not None
                        and weather['dni'] >= self.cfg.minimum_dni
                        and weather['cloud'] <= self.cfg.maximum_cloud)
        target_az = (sun_az + self.az_offset) % 360
        target_el = sun_el + self.el_offset
        readings = self.read_quadrants(target_az, target_el, sun_az, sun_el, weather)
        error = quadrant_error(readings, self.cfg)
        if error is None:
            self.mode = 'SENSOR_INVALID_PREDICTIVE'
            self.filtered = None
            self.low_since = None
            return target_az, target_el, None, weather

        ex, ey, total = error
        if self.filtered is None:
            self.filtered = (ex, ey)
        else:
            a = self.cfg.filter_alpha
            self.filtered = (a*ex + (1-a)*self.filtered[0],
                             a*ey + (1-a)*self.filtered[1])
        fx, fy = self.filtered
        misaligned = abs(fx) > self.cfg.error_deadband or abs(fy) > self.cfg.error_deadband
        if good_weather and misaligned:
            if self.low_since is None:
                self.low_since = t
        else:
            self.low_since = None

        self.mode = 'PREDICTIVE_CLOUD_OR_OFFLINE' if not good_weather else 'TRACKING'
        if good_weather and t - self.last_correction >= self.cfg.correction_interval:
            self.last_correction = t
            if misaligned:
                # Direction and gain below are ONLY correct for this simulator.
                # For hardware, calibrate signs and angular scale; clamp motion.
                self.az_offset = max(-self.cfg.max_offset, min(self.cfg.max_offset,
                    self.az_offset + self.cfg.gain_deg_per_error*fx))
                self.el_offset = max(-self.cfg.max_offset, min(self.cfg.max_offset,
                    self.el_offset + self.cfg.gain_deg_per_error*fy))
                self.mode = 'FINE_CORRECTION_SIMULATED'
        if (self.low_since is not None
            and t - self.low_since >= self.cfg.recalibration_delay
            and t - self.last_recalibration >= self.cfg.recalibration_cooldown):
            # This marks a recalibration request; no physical scanning occurs.
            self.last_recalibration = t
            self.mode = 'RECALIBRATION_REQUEST_SIMULATED'
        target_az = (sun_az + self.az_offset) % 360
        target_el = sun_el + self.el_offset
        return target_az, target_el, (round(fx, 3), round(fy, 3), round(total, 1)), weather


def main():
    p = argparse.ArgumentParser(description='SHINE Python sun tracking (SIMULATION ONLY)')
    p.add_argument('--lat', type=float, required=True)
    p.add_argument('--lon', type=float, required=True)
    p.add_argument('--interval', type=float, default=30.0)
    p.add_argument('--once', action='store_true')
    args = p.parse_args()
    if not (-90 <= args.lat <= 90 and -180 <= args.lon <= 180 and args.interval > 0):
        p.error('invalid latitude, longitude, or interval')
    cfg = Config(args.lat, args.lon, interval=args.interval)
    tracker = Tracker(cfg)
    print('SHINE SIMULATION ONLY — NO MOTOR COMMANDS WILL BE SENT')
    try:
        while True:
            az, el, sensor, weather = tracker.step()
            weather_text = ('unavailable' if weather is None else
                f"DNI={weather['dni']:.0f} cloud={weather['cloud']:.0f}% wind={weather['wind']:.1f}km/h")
            print(f"{datetime.now(timezone.utc).isoformat(timespec='seconds')} "
                  f"mode={tracker.mode} AZ={az:.2f} EL={el:.2f} "
                  f"QPD(x,y,total)={sensor} offsets=({tracker.az_offset:+.2f},"
                  f"{tracker.el_offset:+.2f}) weather={weather_text}")
            if args.once:
                break
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print('\nStopped.')


if __name__ == '__main__':
    main()
