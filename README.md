# esp8266-smart-clock

ESP8266 smart clock is a clock based on an ESP8266 chip (nodemcuv2 by default) connected to a 32x8 LED matrix through
MAX7219 chips (search `MAX7219 32x8` online to see an example).

It displays the time and temperature, repeatedly switching between the two. Time is synced using NTP and the temperature
is gotten from [Tasmota](https://tasmota.github.io/docs/) using MQTT.

## Configuration

Configuring the clock is done in `src/main.cpp`, every value is labeled. For configuring NTP please refer to the README
of [this library](https://github.com/sstaub/NTP).

Tasmota sensor name can be seen on the web UI. In this case sensor name would be `BME280`:

![](https://user-images.githubusercontent.com/5904370/68090360-337c7780-fe73-11e9-95a0-1ec84fae8090.png)

Make sure Tasmota sends the correct JSON message:

```json
{
  "Time": "2019-11-03T19:34:28",
  "BME280": {
    "Temperature": 21.7,
    "Humidity": 66.6,
    "Pressure": 988.6
  },
  "PressureUnit": "hPa",
  "TempUnit": "C"
}
```

(Image and JSON from [Tasmota docs](https://tasmota.github.io/docs/BME280/))

Smart clock will read Sensor.Temperature and TempUnit to display the temperature.

## Wiring

- Matrix CLK <=> ESP8266 SCLK (D5 on nodemcuv2)
- Matrix CS <=> D8 (ESP8266 CS on nodemcuv2)
- Matrix DIN <=> ESP8266 MOSI (D7 on nodemcuv2)