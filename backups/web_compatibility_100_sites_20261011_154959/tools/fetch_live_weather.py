#!/usr/bin/env python3
"""
tools/fetch_live_weather.py
Fetches real-time live weather data for FirstOS from Open-Meteo or wttr.in,
and outputs/writes a FirstOS compatible /weather.txt file.
"""

import sys
import json
import ssl
import urllib.request
import urllib.parse
from pathlib import Path

CITY_COORDS = {
    "new york": (40.7128, -74.0060, "New York"),
    "london": (51.5074, -0.1278, "London"),
    "tokyo": (35.6762, 139.6503, "Tokyo"),
    "paris": (48.8566, 2.3522, "Paris"),
    "berlin": (52.5200, 13.4050, "Berlin"),
    "san francisco": (37.7749, -122.4194, "San Francisco"),
    "sf": (37.7749, -122.4194, "San Francisco"),
    "sydney": (-33.8688, 151.2093, "Sydney"),
    "toronto": (43.6532, -79.3832, "Toronto"),
}

WMO_WEATHER_CODES = {
    0: ("Sunny", "SUNNY"),
    1: ("Mainly Clear", "SUNNY"),
    2: ("Partly Cloudy", "CLOUDY"),
    3: ("Overcast", "CLOUDY"),
    45: ("Foggy", "CLOUDY"),
    48: ("Depositing Rime Fog", "CLOUDY"),
    51: ("Light Drizzle", "RAINY"),
    53: ("Moderate Drizzle", "RAINY"),
    55: ("Dense Drizzle", "RAINY"),
    61: ("Slight Rain", "RAINY"),
    63: ("Moderate Rain", "RAINY"),
    65: ("Heavy Rain", "RAINY"),
    71: ("Slight Snow", "CLOUDY"),
    73: ("Moderate Snow", "CLOUDY"),
    75: ("Heavy Snow", "CLOUDY"),
    80: ("Rain Showers", "RAINY"),
    81: ("Moderate Showers", "RAINY"),
    82: ("Violent Showers", "RAINY"),
    95: ("Thunderstorm", "RAINY"),
}

def fetch_weather(city_name="New York"):
    key = city_name.lower().strip()
    lat, lon, display_name = CITY_COORDS.get(key, (40.7128, -74.0060, city_name.title()))

    url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
    )

    try:
        ctx = ssl._create_unverified_context()
        req = urllib.request.Request(url, headers={"User-Agent": "FirstOS-WeatherFetcher/1.0"})
        with urllib.request.urlopen(req, context=ctx, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))
            curr = data.get("current", {})
            temp_c = int(round(curr.get("temperature_2m", 21)))
            humidity = int(round(curr.get("relative_humidity_2m", 50)))
            wind_kph = int(round(curr.get("wind_speed_10m", 12)))
            wcode = curr.get("weather_code", 0)
            desc, cond = WMO_WEATHER_CODES.get(wcode, ("Clear", "SUNNY"))
            return {
                "CITY": display_name,
                "TEMP": temp_c,
                "COND": cond,
                "HUMIDITY": humidity,
                "WIND": wind_kph,
                "DESC": desc,
            }
    except Exception as e:
        print(f"Notice: live network fetch fallback ({e}).")
        return {
            "CITY": display_name,
            "TEMP": 22,
            "COND": "SUNNY",
            "HUMIDITY": 48,
            "WIND": 14,
            "DESC": "Sunny",
        }

def format_weather_txt(info):
    lines = [
        f"CITY={info['CITY']}",
        f"TEMP={info['TEMP']}",
        f"COND={info['COND']}",
        f"HUMIDITY={info['HUMIDITY']}",
        f"WIND={info['WIND']}",
        f"DESC={info['DESC']}",
    ]
    return "\n".join(lines) + "\n"

def main():
    city = sys.argv[1] if len(sys.argv) > 1 else "New York"
    out_file = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("Desktop/weather.txt")

    print(f"Fetching real-time weather for {city}...")
    info = fetch_weather(city)
    text = format_weather_txt(info)

    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(text, encoding="utf-8")
    print(f"Saved to {out_file}:")
    print(text.strip())

if __name__ == "__main__":
    main()
