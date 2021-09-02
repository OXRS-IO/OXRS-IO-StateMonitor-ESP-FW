# ESP32 state monitor firmware for the Open eXtensible Rack System

A binary state monitor for DIY home automation projects.

This system uses UTP cable (typically Cat-5e because it's cheap) to connect binary sensors to a central controller. The sensors can be buttons or switches mounted in wall plates for lighting control, reed sensors attached to doors or windows, PIR sensors for motion detection, or anything else that reports a binary state.

It also supports rotary encoders (using 2 data lines) to allow up/down control for media player volume, light dimming, etc.

When a sensor state change is detected the USM will publish an MQTT message to the configured MQTT broker for further processing by your home automation system.

Each port on a USM can monitor up to 4 channels and are numbered (for a 24-port USM);

|INDEX|PORT|CHANNEL|TYPE |RJ45 Pin|
|-----|----|-------|-----|--------|
|1    |1   |1      |Input|1       |
|2    |1   |2      |Input|2       |
|3    |1   |3      |Input|3       |
|4    |1   |4      |Input|6       |
|5    |2   |1      |Input|1       |
|6    |2   |2      |Input|2       |
|7    |2   |3      |Input|3       |
|8    |2   |4      |Input|6       |
|...  |    |       |     |        |
|93   |24  |1      |Input|1       |
|94   |24  |2      |Input|2       |
|95   |24  |3      |Input|3       |
|96   |24  |4      |Input|6       |


## Inputs
### Configuration
Each INPUT can be configured by publishing an MQTT message to one of these topics;
```
[BASETOPIC/]conf/CLIENTID/[LOCATION/]INDEX/type
[BASETOPIC/]conf/CLIENTID/[LOCATION/]INDEX/invt
```    
where;
- `BASETOPIC`:   Optional base topic if your topic structure requires it
- `CLIENTID`:    Client id of device, defaults to `usm-<MACADDRESS>`
- `LOCATION`:    Optional location if your topic structure requires it
- `INDEX`:       Index of the input to configure (1-based)
    
The message should be;
- `/type`:       One of `button`, `contact`, `rotary`, `switch` or `toggle`
- `/invt`:       Either `off` or `on` (to invert event)
    
A null or empty message will reset the input to;
- `/type`:       `switch`
- `/invt`:       `off` (non-inverted)
    
A retained message will ensure the USM auto-configures on startup.

**NOTE: inverting a normally-open (NO) button input will result in a constant stream of `hold` events!**

### Events
An input EVENT is reported to a topic of the form;
```
[BASETOPIC/]stat/CLIENTID/[LOCATION/]INDEX
```
where; 
- `BASETOPIC`:   Optional base topic if your topic structure requires it
- `CLIENTID`:    Client id of device, defaults to `usm-<MACADDRESS>`
- `LOCATION`:    Optional location if your topic structure requires it
- `INDEX`:       Index of the input to configure (1-based)

The message is a JSON payload of the form; 
```
{"port":2,"channel":3,"index":7,"type":"contact","event":"open"}
```
where `event` can be one of (depending on type);
- `button`:      `single`, `double`, `triple`, `quad`, `penta`, or `hold`
- `contact`:     `open` or `closed`
- `rotary`:      `up` or `down`
- `switch`:      `on` or `off`
- `toggle`:      `toggle`


## Hardware
The USM firmware is compatible with the [Light Switch Controller](https://github.com/SuperHouse/LSC) (LSC) and is designed to run on the [Universal Rack Controller](https://github.com/SuperHouse/URC) (URC).

The LSC hardware provides 12V power down the line, which can be used for powering sensors (e.g. PIRs), or illuminating LEDs.

The sensors or switches should pull one of 4 INPUT wires in the cable to GND to indicate that they have been activated. 

The LSC hardware has physical pull up resistors to pull the INPUT wires high and detects when they are pulled low.

The RJ45 pinout for each port is;

|PIN|DESCRIPTION|
|---|-----------|
|1  |INPUT 1    |
|2  |INPUT 2    |
|3  |INPUT 3    |
|4  |VIN        |
|5  |VIN        |
|6  |INPUT 4    |
|7  |GND        |
|8  |GND        |

More information:

 * http://www.superhouse.tv/lsc
 * http://www.superhouse.tv/urc


## Credits
 * Jonathan Oxer <jon@oxer.com.au>
 * Ben Jones <https://github.com/sumnerboy12>
 * Moin <https://github.com/moinmoin-sh>


## License
Copyright 2020-2021 SuperHouse Automation Pty Ltd  www.superhouse.tv  

The software portion of this project is licensed under the Simplified
BSD License. The "licence" folder within this project contains a
copy of this license in plain text format.
