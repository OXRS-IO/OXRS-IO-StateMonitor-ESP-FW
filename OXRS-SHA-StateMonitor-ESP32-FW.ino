/**
  ESP32 state monitor firmware for the Open eXtensible Rack System
  
  See https://github.com/SuperHouse/OXRS-SHA-StateMonitor-ESP32-FW for documentation.
  
  Compile options:
    ESP32

  External dependencies. Install using the Arduino library manager:
    "Adafruit_MCP23017"
    "OXRS-SHA-Rack32-ESP32-LIB" by SuperHouse Automation Pty
    "OXRS-SHA-IOHandler-ESP32-LIB" by SuperHouse Automation Pty

  Compatible with the Light Switch Controller hardware found here:
    www.superhouse.tv/lightswitch

  GitHub repository:
    https://github.com/SuperHouse/OXRS-SHA-StateMonitor-ESP32-FW

  Bugs/Features:
    See GitHub issues list
  
  Copyright 2019-2021 SuperHouse Automation Pty Ltd
*/

/*--------------------------- Version ------------------------------------*/
#define FW_NAME       "OXRS-SHA-StateMonitor-ESP32-FW"
#define FW_SHORT_NAME "State Monitor"
#define FW_MAKER_CODE "SHA"
#define FW_MAKER_NAME "SuperHouse Automation"
#define FW_VERSION    "1.2.1"
#define FW_CODE       "osm"

/*--------------------------- Configuration ------------------------------*/
// Should be no user configuration in this file, everything should be in;
#include "config.h"

/*--------------------------- Libraries ----------------------------------*/
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <OXRS_Rack32.h>              // Rack32 support
#include <OXRS_Input.h>               // For input handling

/*--------------------------- Constants ----------------------------------*/
// Can have up to 8x MCP23017s on a single I2C bus
const byte    MCP_I2C_ADDRESS[]     = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27 };
const uint8_t MCP_COUNT             = sizeof(MCP_I2C_ADDRESS);

// Each MCP23017 has 16 I/O pins
#define       MCP_PIN_COUNT         16

/*--------------------------- Global Variables ---------------------------*/
// Each bit corresponds to an MCP found on the IC2 bus
uint8_t g_mcps_found = 0;

/*--------------------------- Instantiate Global Objects -----------------*/
// Rack32 handler
OXRS_Rack32 rack32(FW_NAME, FW_SHORT_NAME, FW_MAKER_CODE, FW_MAKER_NAME, FW_VERSION);

// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// Input handlers
OXRS_Input oxrsInput[MCP_COUNT];

/*--------------------------- Program ------------------------------------*/
/**
  Setup
*/
void setup()
{
  // Set up Rack32 config
  rack32.setMqttBroker(MQTT_BROKER, MQTT_PORT);
  rack32.setMqttAuth(MQTT_USERNAME, MQTT_PASSWORD);
  rack32.setMqttTopicPrefix(MQTT_TOPIC_PREFIX);
  rack32.setMqttTopicSuffix(MQTT_TOPIC_SUFFIX);
  
  // Start Rack32 hardware
  rack32.begin(jsonConfig, NULL);

  // Scan the I2C bus and set up I/O buffers
  scanI2CBus();

  // Set up port display
  rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_INPUT_128);

  // Speed up I2C clock for faster scan rate (after bus scan)
  #ifdef I2C_CLOCK_SPEED
    Serial.print(F("Setting I2C clock speed to "));
    Serial.println(I2C_CLOCK_SPEED);
    Wire.setClock(I2C_CLOCK_SPEED);
  #endif
}

/**
  Main processing loop
*/
void loop()
{
  // Iterate through each of the MCP23017s
  uint32_t port_changed = 0L;
  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    if (bitRead(g_mcps_found, mcp) == 0)
      continue;

    // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcp23017[mcp].readGPIOAB();

    // Show port animations
    rack32.updateDisplayPorts(mcp, io_value);

    // Check for any input events
    oxrsInput[mcp].process(mcp, io_value);
  }

  // Let Rack32 hardware handle any events etc
  rack32.loop();
}

/**
  Config handler
 */
void jsonConfig(JsonObject json)
{
  uint8_t index = getIndex(json);
  if (index == 0) return;

  // Work out the MCP and pin we are configuring
  int mcp = (index - 1) / MCP_PIN_COUNT;
  int pin = (index - 1) % MCP_PIN_COUNT;

  if (json.containsKey("type"))
  {
    if (json["type"].isNull() || strcmp(json["type"], "switch") == 0)
    {
      oxrsInput[mcp].setType(pin, SWITCH);
    }
    else if (strcmp(json["type"], "button") == 0)
    {
      oxrsInput[mcp].setType(pin, BUTTON);
    }
    else if (strcmp(json["type"], "contact") == 0)
    {
      oxrsInput[mcp].setType(pin, CONTACT);
    }
    else if (strcmp(json["type"], "rotary") == 0)
    {
      oxrsInput[mcp].setType(pin, ROTARY);
    }
    else if (strcmp(json["type"], "toggle") == 0)
    {
      oxrsInput[mcp].setType(pin, TOGGLE);
    }
    else 
    {
      Serial.println(F("[erro] invalid input type"));
    }
  }
  
  if (json.containsKey("invert"))
  {
    if (json["invert"].isNull())
    {
      oxrsInput[mcp].setInvert(pin, false);
    }
    else
    {
      oxrsInput[mcp].setInvert(pin, json["invert"].as<bool>());
    }
  }
}

uint8_t getIndex(JsonObject json)
{
  if (!json.containsKey("index"))
  {
    Serial.println(F("[erro] missing index"));
    return 0;
  }
  
  uint8_t index = json["index"].as<uint8_t>();
  
  // Check the index is valid for this device
  if (index <= 0 || index > (MCP_COUNT * MCP_PIN_COUNT))
  {
    Serial.println(F("[erro] invalid index"));
    return 0;
  }

  return index;
}

void publishEvent(uint8_t index, uint8_t type, uint8_t state)
{
  // Calculate the port and channel for this index (all 1-based)
  uint8_t port = ((index - 1) / 4) + 1;
  uint8_t channel = index - ((port - 1) * 4);
  
  char inputType[8];
  getInputType(inputType, type);
  char eventType[7];
  getEventType(eventType, type, state);

  StaticJsonDocument<128> json;
  json["port"] = port;
  json["channel"] = channel;
  json["index"] = index;
  json["type"] = inputType;
  json["event"] = eventType;

  if (!rack32.publishStatus(json.as<JsonObject>()))
  {
    // TODO: add any failover handling in here!
    Serial.println("FAILOVER!!!");    
  }
}

void getInputType(char inputType[], uint8_t type)
{
  // Determine what type of input we have
  sprintf_P(inputType, PSTR("error"));
  switch (type)
  {
    case BUTTON:
      sprintf_P(inputType, PSTR("button"));
      break;
    case CONTACT:
      sprintf_P(inputType, PSTR("contact"));
      break;
    case ROTARY:
      sprintf_P(inputType, PSTR("rotary"));
      break;
    case SWITCH:
      sprintf_P(inputType, PSTR("switch"));
      break;
    case TOGGLE:
      sprintf_P(inputType, PSTR("toggle"));
      break;
  }
}

void getEventType(char eventType[], uint8_t type, uint8_t state)
{
  // Determine what event we need to publish
  sprintf_P(eventType, PSTR("error"));
  switch (type)
  {
    case BUTTON:
      switch (state)
      {
        case HOLD_EVENT:
          sprintf_P(eventType, PSTR("hold"));
          break;
        case 1:
          sprintf_P(eventType, PSTR("single"));
          break;
        case 2:
          sprintf_P(eventType, PSTR("double"));
          break;
        case 3:
          sprintf_P(eventType, PSTR("triple"));
          break;
        case 4:
          sprintf_P(eventType, PSTR("quad"));
          break;
        case 5:
          sprintf_P(eventType, PSTR("penta"));
          break;
      }
      break;
    case CONTACT:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("closed"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("open"));
          break;
      }
      break;
    case ROTARY:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("up"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("down"));
          break;
      }
      break;
    case SWITCH:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("on"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("off"));
          break;
      }
      break;
    case TOGGLE:
      sprintf_P(eventType, PSTR("toggle"));
      break;
  }
}


/**
  Event handlers
*/
void inputEvent(uint8_t id, uint8_t input, uint8_t type, uint8_t state)
{
  // Determine the index for this input event (1-based)
  uint8_t mcp = id;
  uint8_t index = (MCP_PIN_COUNT * mcp) + input + 1;

  // Publish the event
  publishEvent(index, type, state);
}

/**
  I2C
*/
void scanI2CBus()
{
  Serial.println(F("Scanning for MCP23017s on I2C bus..."));

  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    Serial.print(F(" - 0x"));
    Serial.print(MCP_I2C_ADDRESS[mcp], HEX);
    Serial.print(F("..."));

    // Check if there is anything responding on this address
    Wire.beginTransmission(MCP_I2C_ADDRESS[mcp]);
    if (Wire.endTransmission() == 0)
    {
      bitWrite(g_mcps_found, mcp, 1);
      
      // If an MCP23017 was found then initialise and configure the inputs
      mcp23017[mcp].begin_I2C(MCP_I2C_ADDRESS[mcp]);
      for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
      {
        #ifdef MCP_INTERNAL_PULLUPS
          mcp23017[mcp].pinMode(pin, INPUT_PULLUP);
        #else
          mcp23017[mcp].pinMode(pin, INPUT);
        #endif
      }

      // Listen for input events
      oxrsInput[mcp].onEvent(inputEvent);

      Serial.print(F("MCP23017"));
      #ifdef MCP_INTERNAL_PULLUPS
        Serial.print(F(" (internal pullups)"));
      #endif
      Serial.println();
    }
    else
    {
      Serial.println(F("empty"));
    }
  }
}
