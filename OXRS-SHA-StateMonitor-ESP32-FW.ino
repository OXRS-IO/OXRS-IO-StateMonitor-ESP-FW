/**
  ESP32 state monitor firmware for the Open eXtensible Rack System
  
  See https://github.com/SuperHouse/OXRS-SHA-StateMonitor-ESP32-FW for documentation.
  
  Compile options:
    ESP32

  External dependencies. Install using the Arduino library manager:
    "Adafruit_MCP23X17" (requires recent "Adafruit_BusIO" library)
    "PubSubClient" by Nick O'Leary
    "SSD1306Ascii"
    "OXRS-SHA-MQTT-ESP32-LIB" by SuperHouse Automation Pty
    "OXRS-SHA-IOHandler-ESP32-LIB" by SuperHouse Automation Pty

  Bundled dependencies. No need to install separately:
    "USM_Oled" by moinmoin-sh

  Based on the Light Switch Controller hardware found here:
    www.superhouse.tv/lightswitch

  Bugs/Features:
   - See GitHub issues list.

  Written by Ben Jones & Moin on behalf of www.superhouse.tv
    https://github.com/sumnerboy12/
    https://github.com/moinmoin-sh/
    https://github.com/superhouse/

  Copyright 2019-2021 SuperHouse Automation Pty Ltd
*/

/*--------------------------- Version ------------------------------------*/
#define FW_NAME    "OXRS-SHA-StateMonitor-ESP32-FW"
#define FW_VERSION "1.0.0"

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
#include "USM_Oled.h"                 // For OLED runtime displays

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>                     // Also required for Ethernet to get MAC
#endif

/*--------------------------- Constants ----------------------------------*/
// Each MCP23017 has 16 I/O pins
#define MCP_PIN_COUNT 16

/*--------------------------- Global Variables ---------------------------*/
// Each bit corresponds to an MCP found on the IC2 bus
uint8_t g_mcps_found = 0;

// Was an OLED found on the I2C bus
byte g_oled_found = 0;

// Last time the watchdog was reset
uint32_t g_watchdog_last_reset_ms = 0L;

// store MCP portAB values -> needed for port animation
uint16_t g_mcp_io_values[MCP_COUNT];

// for timeout (clear) of bottom line input event display
uint32_t g_last_event_display = 0L;

// for timeout (dim) of OLED
uint32_t g_last_oled_trigger = 0L;

/*--------------------------- Function Signatures ------------------------*/
void mqttCallback(char * topic, byte * payload, int length);

/*--------------------------- Instantiate Global Objects -----------------*/
// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// Input handlers
OXRS_Input oxrsInput[MCP_COUNT];

// Ethernet client
EthernetClient ethernet;

// MQTT client
PubSubClient mqttClient(MQTT_BROKER, MQTT_PORT, mqttCallback, ethernet);
OXRS_MQTT mqtt(mqttClient);

// OLED
SSD1306AsciiWire oled;

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

  // Set up watchdog
  initialiseWatchdog();

  // Start the I2C bus
  Wire.begin();

  // Scan the I2C bus for any MCP23017s and initialise them
  scanI2CBus();

  // Set I2C clock to 400 kHz for faster scan rate  
  // needs to be called after scanI2CBus() to take effect
  Wire.setClock(I2C_CLOCK_SPEED);

  // Display the firmware version and initialise the port display
  if (g_oled_found)
  {
    USM_Oled_draw_logo(FW_VERSION);
    USM_Oled_draw_ports(g_mcps_found);
  }

  // Determine Ethernet MAC address
  byte ethernet_mac[6];
  if (ENABLE_MAC_ADDRESS_ROM)
  {
#ifdef ARDUINO_ARCH_ESP32
    Serial.print(F("Getting Ethernet MAC address from ESP32 Base MAC: "));
    WiFi.macAddress(ethernet_mac);  // Temporarily populate Ethernet MAC with ESP32 Base MAC
    ethernet_mac[5] += 3;           // Ethernet MAC is Base MAC + 3 (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system.html#mac-address)
#else
    Serial.print(F("Getting Ethernet MAC address from external ROM: "));
    ethernet_mac[0] = readRegister(MAC_I2C_ADDRESS, 0xFA);
    ethernet_mac[1] = readRegister(MAC_I2C_ADDRESS, 0xFB);
    ethernet_mac[2] = readRegister(MAC_I2C_ADDRESS, 0xFC);
    ethernet_mac[3] = readRegister(MAC_I2C_ADDRESS, 0xFD);
    ethernet_mac[4] = readRegister(MAC_I2C_ADDRESS, 0xFE);
    ethernet_mac[5] = readRegister(MAC_I2C_ADDRESS, 0xFF);
#endif
  }
  else
  {
    Serial.print(F("Using static MAC address: "));
    memcpy(ethernet_mac, STATIC_MAC, sizeof(ethernet_mac));
  }
  char mac_address[18];
  sprintf_P(mac_address, PSTR("%02X:%02X:%02X:%02X:%02X:%02X"), ethernet_mac[0], ethernet_mac[1], ethernet_mac[2], ethernet_mac[3], ethernet_mac[4], ethernet_mac[5]);
  Serial.println(mac_address);

  // Set up Ethernet
  Ethernet.init(ETHERNET_CS_PIN);
  resetWiznetChip();
  if (ENABLE_DHCP)
  {
    Serial.print(F("Getting IP address via DHCP: "));
    Ethernet.begin(ethernet_mac);
  }
  else
  {
    Serial.print(F("Using static IP address: "));
    Ethernet.begin(ethernet_mac, STATIC_IP, STATIC_DNS);
  }
  Serial.println(Ethernet.localIP());

  // Display IP and MAC addresses
  if (g_oled_found)
  {
    oled.setCursor(25, 2);
    oled.print(Ethernet.localIP()); 
    oled.setCursor(25, 3);
    oled.print(mac_address); 
  }

  // Set up our MQTT connection
  mqtt.setClientId("osm", ethernet_mac);
  mqtt.setAuth(MQTT_USERNAME, MQTT_PASSWORD);
  mqtt.setTopicPrefix(MQTT_TOPIC_PREFIX);
  mqtt.setTopicSuffix(MQTT_TOPIC_SUFFIX);

  // Listen for config messages
  mqtt.onConfig(mqttConfig);
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

  // Iterate through each of the MCP23017 input buffers
  uint32_t port_changed = 0L;
  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    if (bitRead(g_mcps_found, mcp) == 0)
      continue;

    // Read the values for all 16 inputs on this MCP
    uint16_t io_value = mcp23017[mcp].readGPIOAB();

    // Compare with last stored value
    uint16_t tmp = io_value ^ g_mcp_io_values[mcp];
    for (uint8_t j = 0; j < 4; j++)
    {
      if ((tmp >> (j * 4)) & 0x000F)
      {
        bitSet(port_changed, (mcp * 4) + j);
      }
    }

    // Need to store so we can detect changes for port animation
    g_mcp_io_values[mcp] = io_value;

    // Check for any input events
    oxrsInput[mcp].process(mcp, io_value);
  }
  
  // Update OLED port animation 
  updateOled(port_changed);

  // Pat the watchdog once our processing loop completes
  patWatchdog();
}

/**
  MQTT
*/
void mqttCallback(char * topic, byte * payload, int length) 
{
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
    if (json["invert"].isNull() || strcmp(json["invert"], "off"))
    {
      oxrsInput[mcp].setInvert(pin, 0);
    }
    else if (strcmp(json["invert"], "on"))
    {
      oxrsInput[mcp].setInvert(pin, 1);
    }
    else 
    {
      Serial.println(F("[erro] invalid invert"));
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

  if (g_oled_found)
  {
    // Show input event on buttom line
    char event[32];
    sprintf_P(event, PSTR("IDX:%2d %s %s   "), index, inputType, eventType);

    oled.setCursor(0, 7);
    oled.setInvertMode(true);
    oled.print(event); 
    oled.setInvertMode(false);    
    g_last_event_display = millis(); 
  }

  // Build JSON payload for this event
  StaticJsonDocument<128> json;
  json["port"] = port;
  json["channel"] = channel;
  json["index"] = index;
  json["type"] = inputType;
  json["event"] = eventType;

  // Publish to MQTT
  if (!mqtt.publishStatus(json.as<JsonObject>()))
  {
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
  OLED
*/
void updateOled(uint32_t port_changed)
{
  if (g_oled_found)
  {
    // Check if port animation update required
    if (port_changed)
    {
      oled.ssd1306WriteCmd(SSD1306_DISPLAYON);
      oled.setContrast(OLED_CONTRAST_ON);
      g_last_oled_trigger = millis(); 
 
      USM_Oled_animate(port_changed, g_mcp_io_values);
    }

    // Clear event display if timed out
    if (g_last_event_display)
    {
      if ((millis() - g_last_event_display) > OLED_EVENT_MS)
      {
        oled.setCursor(0, 7);
        oled.clearToEOL();
        
        g_last_event_display = 0L;
      }
    }

    // Dim OLED if timed out
    if (g_last_oled_trigger)
    {
      if ((millis() - g_last_oled_trigger) > OLED_ON_MS)
      {
        // Turn OLED OFF if OLED_CONTRAST_DIM is set to 0
        if (OLED_CONTRAST_DIM == 0)
        {
          oled.ssd1306WriteCmd(SSD1306_DISPLAYOFF);
        }
        else
        { 
          oled.setContrast(OLED_CONTRAST_DIM);
        }
        
        g_last_oled_trigger = 0L;
      }
    }
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
  Watchdog
*/
void initialiseWatchdog()
{
  if (ENABLE_WATCHDOG)
  {
    Serial.print(F("Watchdog enabled on pin "));
    Serial.println(WATCHDOG_PIN);

    pinMode(WATCHDOG_PIN, OUTPUT);
    digitalWrite(WATCHDOG_PIN, LOW);
  }
  else
  {
    Serial.println(F("Watchdog NOT enabled"));
  }
}

void patWatchdog()
{
  if (ENABLE_WATCHDOG)
  {
    if ((millis() - g_watchdog_last_reset_ms) > WATCHDOG_RESET_MS)
    {
      digitalWrite(WATCHDOG_PIN, HIGH);
      delay(WATCHDOG_PULSE_MS);
      digitalWrite(WATCHDOG_PIN, LOW);

      g_watchdog_last_reset_ms = millis();
    }
  }
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

  // Scan for OLED   
  Serial.print(F(" - 0x"));
  Serial.print(OLED_I2C_ADDRESS, HEX);
  Serial.print(F("..."));

  // Check if OLED is anything responding
  Wire.beginTransmission(OLED_I2C_ADDRESS);
  if (Wire.endTransmission() == 0)
  {
     g_oled_found = 1;
    
    // If OLED was found then initialise
    #ifdef OLED_TYPE_SSD1306
      Serial.print(F("SSD1306 "));
      #if OLED_RESET_PIN >= 0
        oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS, OLED_RESET_PIN);
      #else
        oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
      #endif
    #endif
    #ifdef OLED_TYPE_SH1106
      Serial.print(F("SH1106 "));
      #if OLED_RESET_PIN >= 0
        oled.begin(&SH1106_128x64, OLED_I2C_ADDRESS, OLED_RESET_PIN);
      #else
        oled.begin(&SH1106_128x64, OLED_I2C_ADDRESS);
      #endif
    #endif
    
    oled.clear();
    oled.setFont(Adafruit5x7);  
    oled.println(F("Initialising..."));
    
    Serial.println(F("OLED"));
  }
  else
  {
    Serial.println(F("empty"));
  }
}

// Read 1 byte from I2C device register
uint8_t readRegister(int adr, uint8_t reg) 
{
  Wire.beginTransmission(adr);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(adr, 1);
  while (!Wire.available()) {}
  return Wire.read();
}

// Reset the Wiznet Ethernet chip
void resetWiznetChip()
{
#ifdef WIZNET_RESET_PIN
  Serial.print("Resetting Wiznet W5500 Ethernet chip...  ");
  pinMode(WIZNET_RESET_PIN, OUTPUT);
  digitalWrite(WIZNET_RESET_PIN, HIGH);
  delay(250);
  digitalWrite(WIZNET_RESET_PIN, LOW);
  delay(50);
  digitalWrite(WIZNET_RESET_PIN, HIGH);
  delay(350);
  Serial.println("Done.");
#endif
}
