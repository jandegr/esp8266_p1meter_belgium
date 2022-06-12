# esp8266_p1meter_belgium

Software for the ESP2866 that sends P1 smart meter data to an mqtt broker (with OTA firmware updates)

## about this fork
This fork from https://github.com/daniel-jong/esp8266_p1meter (tries) to add support for the Belgian smart meters which are differently configured than their dutch counterparts.
Wat info van Fluvius //https://www.fluvius.be/sites/fluvius/files/2020-01/dmk-demo-v2.1-rtc.pdf

# Getting started
This setup requires:
- An esp8266 (nodeMcu and Wemos d1 mini have been tested)
- A 10k ohm resistor
- A 4 pin RJ11 or [6 pin RJ12 cable](https://www.tinytronics.nl/shop/nl/kabels/adapters/rj12-naar-6-pins-dupont-jumper-adapter) Both cables work great, but a 6 pin cable can also power the esp8266 on most DSMR5+ meters.

Compiling up using Arduino IDE:
- Ensure you have selected the right board
- Using the Tools->Manage Libraries... install `PubSubClient` and `WifiManager`
- In the file `Settings.h` change `OTA_PASSWORD` to a safe secret value
- Flash the software

Compiling up using PlatformIO:
- Ensure the correct board type is selected in project configuration
- In the file `Settings.h` change `OTA_PASSWORD` to a safe secret value
- Upload the software.

Finishing off:
- You should now see a new wifi network `ESP******` connect to this wifi network, a popup should appear, else manually navigate to `192.168.4.1`
- Configure your wifi and Mqtt settings
- To check if everything is up and running you can listen to the MQTT topic `hass/status`, on startup a single message is sent.

## Connecting to the P1 meter
Connect the esp8266 to an RJ11 cable/connector following the diagram.

| P1 pin   | ESP8266 Pin |
| ----     | ---- |
| 2 - RTS  | 3.3v |
| 3 - GND  | GND  |
| 4 -      |      |
| 5 - RXD (data) | RX (gpio3) |

On most Landys and Gyr models a 10K resistor should be used between the ESP's 3.3v and the p1's DATA (RXD) pin. Many howto's mention RTS requires 5V (VIN) to activate the P1 port, but for me 3V3 suffices.

![Wiring](https://github.com/jandegr/esp8266_p1meter_belgium/blob/main/assets/esp8266_p1meter_bb.png)

### Optional: Powering the esp8266 using your DSMR5+ meter 
<p>
When using a RJ12 cable you can use the power source provided by the meter.
  
| P1 pin   | ESP8266 Pin |
| ----     | ---- |
| 1 - 5v out | 5v or Vin |
| 2 - RTS  |  |
| 3 - GND  | GND  |
| 4 -      |      |
| 5 - RXD (data) | RX (gpio3) |
| 6 - GND  | GND  |

![Wiring powered by meter](https://github.com/jandegr/esp8266_p1meter_belgium/blob/main/assets/P1_powered_by_meter.png)

</p>

## Data Sent

All metrics are send to their own MQTT topic.
The software sends out to the following MQTT topics:

```
sensors/power/p1meter/consumption_low_tarif 2209397
sensors/power/p1meter/consumption_high_tarif 1964962
sensors/power/p1meter/returndelivery_low_tarif 2209397
sensors/power/p1meter/returndelivery_high_tarif 1964962
sensors/power/p1meter/actual_consumption 313
sensors/power/p1meter/actual_returndelivery 0
sensors/power/p1meter/l1_instant_power_usage 313
sensors/power/p1meter/l2_instant_power_usage 0
sensors/power/p1meter/l3_instant_power_usage 0
sensors/power/p1meter/l1_instant_power_current 1000
sensors/power/p1meter/l2_instant_power_current 0
sensors/power/p1meter/l3_instant_power_current 0
sensors/power/p1meter/l1_voltage 233
sensors/power/p1meter/l2_voltage 0
sensors/power/p1meter/l3_voltage 0
sensors/power/p1meter/gas_meter_m3 968922
sensors/power/p1meter/actual_tarif_group 2
sensors/power/p1meter/short_power_outages 3
sensors/power/p1meter/long_power_outages 1
sensors/power/p1meter/short_power_drops 0
sensors/power/p1meter/short_power_peaks 0
```

## Home Assistant Configuration

Use this [example](https://github.com/KurtKeunen/esp8266_p1meter_belgium/blob/main/assets/p1_sensors.yaml) for home assistant's `sensor.yaml`

## Thanks to

This sketch is mostly copied and pasted from daniel-jong and based on jensd's work.
Thanks and credits to them!

- https://github.com/daniel-jong/esp8266_p1meter
- https://jensd.be/1205/linux/data-lezen-van-de-belgische-digitale-meter-met-de-p1-poort

