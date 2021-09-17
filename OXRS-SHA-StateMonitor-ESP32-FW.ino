/**
  ESP32 state monitor firmware for the Open eXtensible Rack System
  
  See https://github.com/SuperHouse/OXRS-SHA-StateMonitor-ESP32-FW for documentation.
  
  Compile options:
    ESP32

  External dependencies. Install using the Arduino library manager:
    "Adafruit_MCP23X17" (requires recent "Adafruit_BusIO" library)
    "PubSubClient" by Nick O'Leary
    "OXRS-IO-MQTT-ESP32-LIB" by OXRS Core Team
    "OXRS-IO-LCD-ESP32-LIB" by OXRS Core Team
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
#define FW_CODE       "osm"
#define FW_VERSION    "1.1.0"
#define FW_SHORT_NAME "State Monitor"
#define FW_MAKER_CODE "IO"
#define FW_PLATFORM   "ESP32"

/*--------------------------- Configuration ------------------------------*/
// Should be no user configuration in this file, everything should be in;
#include "config.h"

/*--------------------------- Libraries ----------------------------------*/
#include <Wire.h>                     // For I2C
#include <Ethernet.h>                 // For networking
#include <PubSubClient.h>             // For MQTT
#include <OXRS_MQTT.h>                // For MQTT
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <OXRS_Input.h>               // For input handling
#include <OXRS_LCD.h>                 // For LCD runtime displays

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>                     // Also required for Ethernet to get MAC
#endif

/*--------------------------- Constants ----------------------------------*/
// Each MCP23017 has 16 I/O pins
#define MCP_PIN_COUNT 16

/*--------------------------- Global Variables ---------------------------*/
// Each bit corresponds to an MCP found on the IC2 bus
uint8_t g_mcps_found = 0;

// temperature update interval timer
uint32_t g_last_temp_update = -TEMP_UPDATE_INTERVAL_MS;

/*--------------------------- Function Signatures ------------------------*/
void mqttCallback(char * topic, byte * payload, int length);

/*--------------------------- Instantiate Global Objects -----------------*/
// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// Input handlers
OXRS_Input oxrsInput[MCP_COUNT];

// Ethernet client
EthernetClient ethernet;

// LCD screen
OXRS_LCD screen(Ethernet);

// MQTT client
PubSubClient mqttClient(MQTT_BROKER, MQTT_PORT, mqttCallback, ethernet);
OXRS_MQTT mqtt(mqttClient);

/*--------------------------- Program ------------------------------------*/
/**
  Setup
*/
void setup()
{
  // Startup logging to serial
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println();
  Serial.println(F("==============================="));
  Serial.println(F("     OXRS by SuperHouse.tv"));
  Serial.println(FW_NAME);
  Serial.print  (F("            v"));
  Serial.println(FW_VERSION);
  Serial.println(F("==============================="));

  // Start the I2C bus
  Wire.begin();

  // Scan the I2C bus and set up I/O buffers
  scanI2CBus();

  // Set up the screen
  screen.begin();
  
  // Speed up I2C clock for faster scan rate (after bus scan)
  Wire.setClock(I2C_CLOCK_SPEED);

  // Display the header and initialise the port display
  screen.draw_header(FW_MAKER_CODE, FW_SHORT_NAME, FW_VERSION, FW_PLATFORM);
  screen.draw_ports(PORT_LAYOUT_INPUT_128, g_mcps_found);

  // Set up ethernet and obtain an IP address
  byte mac[6];
  initialiseEthernet(mac);
  
  // Set up connection to MQTT broker
  initialiseMqtt(mac);
}

/**
  Main processing loop
*/
void loop()
{
  // Check our DHCP lease is still ok
  Ethernet.maintain();

  // Check our MQTT broker connection is still ok
  mqtt.loop();

  // Iterate through each of the MCP23017s
  uint32_t port_changed = 0L;
  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    if (bitRead(g_mcps_found, mcp) == 0)
      continue;

    // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcp23017[mcp].readGPIOAB();

    // Show port animation
    screen.process(mcp, io_value);
    
    // Check for any input events
    oxrsInput[mcp].process(mcp, io_value);
  }

  // Check for temperature update
  updateTemperature ();
  
  // Maintain screen
  screen.loop();
}

void updateTemperature()
{
  if ((millis() - g_last_temp_update) > TEMP_UPDATE_INTERVAL_MS)
  {
    // TODO: read temp from onboard sensor
    float temperature;
    temperature = random(0, 10000) / 100.0;

    // Display temp on screen
    screen.show_temp(temperature); 

    // Publish temp to mqtt
    publishTemperature(temperature);
    
    g_last_temp_update = millis();
  }
}

/**
  MQTT
*/
void initialiseMqtt(byte * mac)
{
  // Set the MQTT client id to the f/w code + MAC address
  mqtt.setClientId(FW_CODE, mac);

  // Set credentials and any extra topic parts specified
  if (MQTT_USERNAME     != NULL) { mqtt.setAuth(MQTT_USERNAME, MQTT_PASSWORD); }
  if (MQTT_TOPIC_PREFIX != NULL) { mqtt.setTopicPrefix(MQTT_TOPIC_PREFIX); }
  if (MQTT_TOPIC_SUFFIX != NULL) { mqtt.setTopicSuffix(MQTT_TOPIC_SUFFIX); }

  // Display the MQTT topic on screen
  char topic[64];
  screen.show_MQTT_topic(mqtt.getWildcardTopic(topic));
  
  // Listen for config messages
  mqtt.onConfig(mqttConfig);
}

void mqttCallback(char * topic, byte * payload, int length) 
{
  // Indicate we have received something on MQTT
  screen.trigger_mqtt_rx_led();
  
  // Pass this message down to our MQTT handler
  mqtt.receive(topic, payload, length);
}

void mqttConfig(JsonObject json)
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
  uint8_t channel = ((index - 1) % 16) + 1;
  
  char inputType[8];
  getInputType(inputType, type);
  char eventType[7];
  getEventType(eventType, type, state);

  // Show event on screen
  char display[32];
  sprintf_P(display, PSTR("idx:%2d %s %s   "), index, inputType, eventType);
  screen.show_event(display);

  // Build JSON payload for this event
  StaticJsonDocument<128> json;
  json["port"] = port;
  json["channel"] = channel;
  json["index"] = index;
  json["type"] = inputType;
  json["event"] = eventType;

  // Publish to MQTT
  if (mqtt.publishStatus(json.as<JsonObject>()))
  {
    // Indicate we have sent something on MQTT
    screen.trigger_mqtt_tx_led();
  }
  else
  {
    // TODO: add any failover handling in here!
    Serial.println("FAILOVER!!!");    
  }
}

void publishTemperature(float temperature)
{
  char payload[8];
  sprintf(payload, "%2.2f", temperature);

  // Build JSON payload for this event
  StaticJsonDocument<64> json;
  json["temperature"] = payload;
  
  // Publish to MQTT
  if (mqtt.publishTelemetry(json.as<JsonObject>()))
  {
    // Indicate we have sent something on MQTT
    screen.trigger_mqtt_tx_led();
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
  Serial.println(F("Scanning for devices on the I2C bus..."));

  // Scan for MCP's
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
        mcp23017[mcp].pinMode(pin, MCP_INTERNAL_PULLUPS ? INPUT_PULLUP : INPUT);
      }

      // Listen for input events
      oxrsInput[mcp].onEvent(inputEvent);

      Serial.print(F("MCP23017"));
      if (MCP_INTERNAL_PULLUPS) { Serial.print(F(" (internal pullups)")); }
      Serial.println();
    }
    else
    {
      // No MCP found at this address
      Serial.println(F("empty"));
    }
  }
}

/**
  Ethernet
 */
void initialiseEthernet(byte * ethernet_mac)
{
  // Determine MAC address
#if ARDUINO_ARCH_ESP32
  Serial.print(F("Getting Ethernet MAC address from ESP32: "));
  WiFi.macAddress(ethernet_mac);  // Temporarily populate Ethernet MAC with ESP32 Base MAC
  ethernet_mac[5] += 3;           // Ethernet MAC is Base MAC + 3 (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#mac-address)
#else
  Serial.print(F("Using hardcoded MAC address: "));
  ethernet_mac[0] = 0xDE;
  ethernet_mac[1] = 0xAD;
  ethernet_mac[2] = 0xBE;
  ethernet_mac[3] = 0xEF;
  ethernet_mac[4] = 0xFE;
  ethernet_mac[5] = 0xED;
#endif

  // Display MAC address on serial
  char mac_address[18];
  sprintf_P(mac_address, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), ethernet_mac[0], ethernet_mac[1], ethernet_mac[2], ethernet_mac[3], ethernet_mac[4], ethernet_mac[5]);
  Serial.println(mac_address);

  // Set up Ethernet
#ifdef ETHERNET_CS_PIN
  Ethernet.init(ETHERNET_CS_PIN);
#endif

  // Reset the Wiznet Ethernet chip
#ifdef WIZNET_RESET_PIN
  Serial.print("Resetting Wiznet W5500 Ethernet chip...");
  pinMode(WIZNET_RESET_PIN, OUTPUT);
  digitalWrite(WIZNET_RESET_PIN, HIGH);
  delay(250);
  digitalWrite(WIZNET_RESET_PIN, LOW);
  delay(50);
  digitalWrite(WIZNET_RESET_PIN, HIGH);
  delay(350);
  Serial.println("done");
#endif

  // Obtain IP address
  Serial.print(F("Getting IP address via DHCP: "));
  Ethernet.begin(ethernet_mac, 10000);

  // Display IP address on serial
  Serial.println(Ethernet.localIP());
}
