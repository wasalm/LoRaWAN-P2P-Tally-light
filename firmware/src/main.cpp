#include <SPI.h>
#include <LoRa.h>
#define FASTLED_ESP8266_RAW_PIN_ORDER
#include <FastLED.h>
#include <Preferences.h>
#include "LoRaWanP2P.h"

// We can only use a single channel
#define FREQUENCY 868100000 // LoRa Frequency
#define SPREADING_FACTOR 9  // DR3
uint8_t devAddr[4] = {0x00, 0x98, 0x13, 0x59};
uint8_t devEUI[8] = {0x81, 0x49, 0xd6, 0x56, 0x2d, 0xea, 0xff, 0x58};
uint8_t appEUI[8] = {0x98, 0x9f, 0xfc, 0x2d, 0x10, 0xfd, 0xbd, 0x11};
uint8_t appKey[16] = {0xed, 0x15, 0x2d, 0x83, 0xe2, 0xb9, 0xff, 0x1f,
                      0xf0, 0x88, 0x25, 0x91, 0x2f, 0x0f, 0xee, 0xa5};

// Colors
#define COLOR_BOOT 0x7F5500
#define COLOR_CONSTANT_OPEN 0x000F4F
#define COLOR_BATTERY 0xff0000
#define COLOR_DOOR 0x50FF00

#define LOW_BATTERY_VOLTAGE 2100
#define CONSTANT_OPEN_TIME 30000 // Time door can stay open, before light fades on

// Led strip config
#define NUM_LEDS 44 // Number of leds on strip

// Pinout config
#define LORA_CS_PIN 15   // D8
#define LORA_RESET_PIN 5 // D1
#define LORA_IRQ_PIN 4   // D2
#define WS2812B_PIN 16   // D0

// Persistent Storage
Preferences prefs;

// Led strip variables
CRGB leds[NUM_LEDS];

// LoRa (not LoRaWAN!) Variables
bool msgAvailable = false;
uint32_t msgTime = 0;
uint8_t msg[64];
int msgLen = 0;
LoRaWanP2P loRaWAN;

unsigned long lastJoined;
unsigned long doorOpened;
bool doorState = false;
bool ledState = false;
unsigned int battVoltage;

void handleLDS02(uint8_t *buf, uint8_t len)
{
  if (len != 10)
  {
    // Invalid msg;
    return;
  }

  battVoltage = ((buf[0] << 8 | buf[1]) & 0x3FFF);
  bool state = buf[0] & 0x80;

  if (state == doorState)
  {
    // State has not changed, don't bother the user.
    return;
  }

  Serial.print("Batt(mV)s: ");
  Serial.println(battVoltage);

  if (state)
  {
    Serial.println("Door opened.");
    doorState = true;
    doorOpened = millis(); // Set timer for constant light to turn on
  }
  else
  {
    Serial.println("Door closed.");
    doorState = false;
  }
}

/*
 * LoRa functions
 */
void LoRa_rxMode()
{
  LoRa.disableInvertIQ(); // normal mode
  LoRa.receive();         // set receive mode
}

void LoRa_txMode()
{
  LoRa.idle();           // set standby mode
  LoRa.enableInvertIQ(); // active invert I and Q signals
}

void onTxDone()
{
  Serial.println("onTxDone");
  LoRa_rxMode();
}

/*
 * LoRaWAN Callbacks
 */

void LoRaWAN_onSave()
{
  // If nothing changes, the library automatically stops copying.
  prefs.putBytes("AppSKey", &loRaWAN.appSKey[0], 16);
  prefs.putBytes("NwkSKey", &loRaWAN.nwkSKey[0], 16);

  prefs.putUInt("FCntDown", loRaWAN.fCntDown >> 7);
  prefs.putUInt("FCntUp", loRaWAN.fCntUp >> 7);
}

void LoRaWAN_onJoin()
{
  Serial.println("Device joined to the system");
}

void LoRaWAN_onMessage(uint8_t port, uint8_t *msg, uint8_t length)
{
  Serial.print("Payload: ");
  for (int i = 0; i < length; i++)
  {
    if (msg[i] < 16)
    {
      Serial.print("0");
    }
    Serial.print(msg[i], HEX);
  }
  Serial.println();

  handleLDS02(msg, length);
}

void LoRaWAN_onResponse(uint8_t *buffer, uint8_t length, uint32_t rxDelay)
{
  LoRa_txMode(); // set tx mode

  unsigned int diff = millis() - msgTime;
  if (diff < rxDelay)
  {
    delay(rxDelay - diff);
  }

  LoRa.beginPacket();
  LoRa.write(buffer, length);
  LoRa.endPacket(true);
}

/*
 * Led function
 */
void handleLights()
{

  /*
   * Blink during boot
   */

  if (millis() < 1000 && !ledState)
  {
    FastLED.showColor(COLOR_BOOT);
    FastLED.showColor(COLOR_BOOT);
    ledState = true;
    return;
  }

  /*
   * Door closes
   */

  if (millis() > 2500 && !doorState && ledState)
  {
    FastLED.showColor(CRGB::Black);
    FastLED.showColor(CRGB::Black);
    ledState = false;
  }

  /*
   * Door open
   *
   * 0000-1000 on
   * 1000-2000 off
   * 2000-3000 on
   * 3000-4000 off
   * 4000-5000 on
   * 5000-.... off
   */

  if (doorState && !ledState && (millis() - doorOpened) < 5000)
  {
    if ((millis() - doorOpened) > 1000 && (millis() - doorOpened) < 2000)
    {
      return;
    }

    if ((millis() - doorOpened) > 3000 && (millis() - doorOpened) < 4000)
    {
      return;
    }

    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds[i] = COLOR_DOOR;
      if (battVoltage < LOW_BATTERY_VOLTAGE)
      {
        leds[NUM_LEDS - 1] = COLOR_BATTERY;
        leds[NUM_LEDS - 2] = COLOR_BATTERY;
        leds[NUM_LEDS - 3] = COLOR_BATTERY;
      }
    }
    FastLED.show();
    FastLED.show();
    ledState = true;
    return;
  }

  if (doorState && ledState && (millis() - doorOpened) < 6000)
  {
    if ((millis() - doorOpened) < 1000)
    {
      return;
    }

    if ((millis() - doorOpened) > 2000 && (millis() - doorOpened) < 3000)
    {
      return;
    }

    if ((millis() - doorOpened) > 4000 && (millis() - doorOpened) < 5000)
    {
      return;
    }

    FastLED.showColor(CRGB::Black);
    FastLED.showColor(CRGB::Black);
    ledState = false;
    return;
  }

  /*
   * Door Stays open
   */

  if (doorState && !ledState && (millis() - doorOpened) > CONSTANT_OPEN_TIME)
  {
    Serial.println("Door stayed open");

    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds[i] = COLOR_CONSTANT_OPEN;
      if (battVoltage < LOW_BATTERY_VOLTAGE)
      {
        leds[NUM_LEDS - 1] = COLOR_BATTERY;
        leds[NUM_LEDS - 2] = COLOR_BATTERY;
        leds[NUM_LEDS - 3] = COLOR_BATTERY;
      }
    }
    FastLED.show();
    FastLED.show();
    ledState = true;
    return;
  }
}

void loop()
{
  if (msgAvailable)
  {
    msgAvailable = false;
    loRaWAN.parseMessage(&msg[0], msgLen, LoRa.packetRssi());
  }

  handleLights();
  Serial.println(millis());
  Serial.println(LoRa.available());
}

void onReceive(int packetSize)
{
  msgTime = millis();

  Serial.print("Receive msg: ");
  msgLen = 0;
  while (LoRa.available())
  {
    if (msgLen < 64)
    {
      msg[msgLen] = LoRa.read();

      Serial.print(msg[msgLen] < 16 ? "0" : "");
      Serial.print(msg[msgLen], HEX);

      msgLen++;
    }
  }
  Serial.println();

  msgAvailable = true;
}

void setup()
{
  Serial.begin(115200); // Initialize serial for debugging
  while (!Serial)
    ;

  // Disable onboard led
  randomSeed(analogRead(0));
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Setup led strip
  FastLED.addLeds<WS2812B, WS2812B_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setTemperature(Tungsten100W);
  FastLED.setBrightness(255);

  prefs.begin("LoRaWAN");

  // Setup LoraWAN
  memcpy(&loRaWAN.devAddr, devAddr, 4);
  memcpy(&loRaWAN.devEUI, devEUI, 8);
  memcpy(&loRaWAN.appEUI, appEUI, 8);
  memcpy(&loRaWAN.appKey, appKey, 16);

  // Load from persistent storage
  memset(&loRaWAN.appSKey[0], 0, 16);
  memset(&loRaWAN.nwkSKey[0], 0, 16);
  prefs.getBytes("AppSKey", &loRaWAN.appSKey[0], 16);
  prefs.getBytes("NwkSKey", &loRaWAN.nwkSKey[0], 16);

  loRaWAN.fCntDown = (prefs.getUInt("FCntDown", 0) + 1) << 7; // ignore the last 128 bits
  loRaWAN.fCntUp = prefs.getUInt("FCntUp", 0) << 7;

  loRaWAN.onSave(LoRaWAN_onSave);
  loRaWAN.onJoin(LoRaWAN_onJoin);
  loRaWAN.onMessage(LoRaWAN_onMessage);
  loRaWAN.onResponse(LoRaWAN_onResponse);

  // Setup LoRa
  LoRa.setPins(LORA_CS_PIN, LORA_RESET_PIN, LORA_IRQ_PIN);
  if (!LoRa.begin(FREQUENCY))
  {
    Serial.println("LoRa init failed. Check your connections.");
    while (true)
      ; // if failed, do nothing
  }
  LoRa.setSpreadingFactor(SPREADING_FACTOR);
  LoRa.setSyncWord(0x34);
  LoRa.onReceive(onReceive);
  LoRa.onTxDone(onTxDone);

  Serial.println("LoRa init succeeded.");
  LoRa_rxMode();
}
