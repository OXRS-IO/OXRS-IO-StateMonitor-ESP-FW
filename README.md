# ESP32 state monitor firmware for the Open eXtensible Rack System

A binary state monitor for DIY home automation projects.

This system uses UTP cable (typically Cat-5e because it's cheap) to connect binary sensors to a central controller. The sensors can be buttons or switches mounted in wall plates for lighting control, reed sensors attached to doors or windows, PIR sensors for motion detection, or anything else that reports a binary state.

It also supports rotary encoders (using 2 data lines) to allow up/down control for media player volume, light dimming, etc.

When an input state change is detected an MQTT message is published to the configured MQTT broker for further processing by your home automation system.

Each port can monitor up to 4 channels and are numbered;

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

The firmware is designed to run on hardware using MCP23017 I/O buffer chips via I2C, e.g. the [Light Switch Controller](https://github.com/SuperHouse/LSC). A single I2C bus can only support up to a maximum of 8x MCP23017 chips (addresses `0x20-0x27`). Therefore the maximum number of supported inputs is 128 (i.e. 8x MCP23017s * 16x I/O pins), or 32 ports.

## Inputs
### Configuration
Each INPUT can be configured by publishing an MQTT message to this topic;
```
[PREFIX/]conf/CLIENTID[/SUFFIX]
```    
where;
- `PREFIX`:   Optional topic prefix if required
- `CLIENTID`: Client id of device, defaults to `osm-<MACADDRESS>`
- `SUFFIX`:   Optional topic suffix if required
    
The message payload should be JSON and contain;
- `index`:    Mandatory, the index of the input to configure
- `type`:     Optional, either `button`, `contact`, `rotary`, `switch` or `toggle`
- `invert`:   Optional, either `on` or `off`
    
A null or empty value will reset the configuration to;
- `type`:     `switch`
- `invert`:   `off` (non-inverted)

#### Examples
To configure input 4 to be a contact sensor;
``` js
{ "input": 4, "type": "contact" }
```

To configure input 7 to be an inverted button;
``` js
{ "input": 7, "type": "button", "invert": "on" }
```

A retained message will ensure the device auto-configures on startup.

**NOTE: inverting a normally-open (NO) button input will result in a constant stream of `hold` events!**

### Events
An input EVENT is reported to a topic of the form;
```
[PREFIX/]stat/CLIENTID[/SUFFIX]
```
where; 
- `PREFIX`:   Optional topic prefix if required
- `CLIENTID`: Client id of device, defaults to `osm-<MACADDRESS>`
- `SUFFIX`:   Optional topic suffix if required

The message payload is JSON and contains; 
- `port`:     The port this event occured on (1-32)
- `channel`:  The channel this event occured on (1-4)
- `index`:    The index of this event (1-128)
- `type`:     Either `button`, `contact`, `rotary`, `switch` or `toggle`
- `event`:    The type of event (see below)

Where `event` can be one of (depending on type);
- `button`:   `single`, `double`, `triple`, `quad`, `penta`, or `hold`
- `contact`:  `open` or `closed`
- `rotary`:   `up` or `down`
- `switch`:   `on` or `off`
- `toggle`:   `toggle`

#### Examples
A contact opening on input 7;
``` js
{ "port": 2, "channel": 3, "index": 7, "type": "contact", "event": "open" }
```

A triple button click on input 4;
``` js
{ "port": 1, "channel": 4, "index": 4, "type": "button", "event": "triple" }
```

## Hardware
This firmware is compatible with the [Light Switch Controller](https://github.com/SuperHouse/LSC) (LSC) and is designed to run on the [RACK32](https://github.com/SuperHouse/RACK32) as part of the [OXRS](https://oxrs.io) eco-system.

The LSC hardware provides 12V power down the cable, which can be used for powering sensors (e.g. PIRs), or illuminating LEDs.

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
