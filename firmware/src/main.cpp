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
uint8_t appSKey[16] = {0x3e, 0x3e, 0x4c, 0x4b, 0xe1, 0xa6, 0x91, 0x12,
                       0xa2, 0xa2, 0x86, 0x37, 0x9a, 0xd6, 0x34, 0x14};
uint8_t nwkSKey[16] = {0xef, 0x9c, 0x2a, 0x59, 0xaa, 0x21, 0x45, 0xeb,
                       0x41, 0xac, 0x61, 0xf4, 0xd3, 0x21, 0xe9, 0x1f};

// Colors
#define COLOR_BOOT 0x7F5500
#define COLOR_BATTERY 0xff0000
#define COLOR_DOOR 0x50FF00

#define LOW_BATTERY_VOLTAGE 2200

// Led strip config
#define NUM_LEDS 44 // Number of leds on strip

// Pinout config
#define LORA_CS_PIN 15    // D8
#define LORA_RESET_PIN 16 // D0
#define LORA_IRQ_PIN 4    // D2
#define WS2812B_PIN 0     // D3

// Persistent Storage
Preferences prefs;

// Led strip variables
CRGB leds[NUM_LEDS];

// LoRa (not LoRaWAN!) Variables
bool msgAvailable = false;
bool sendingDone = false;
uint32_t msgTime = 0;
uint8_t msg[64];
int msgLen = 0;
LoRaWanP2P loRaWAN;

unsigned long lastJoined;
unsigned long doorOpened;
bool doorState = false;
bool ledState = false;
unsigned int battVoltage;

uint32_t prevFCntUp;
uint32_t prevFCntDown;
bool firstMsg = true;

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
  sendingDone = true;
}

void onReceive(int packetSize)
{
  msgTime = millis();
  msgAvailable = true;
}

/*
 * LoRaWAN Callbacks
 */

void LoRaWAN_onSave()
{
  // If nothing changes, the library automatically stops copying.
  uint32_t newFCntUp = loRaWAN.fCntUp >> 7;
  uint32_t newFCntDown = loRaWAN.fCntDown >> 7;

  if (newFCntUp != prevFCntUp || newFCntDown != prevFCntDown)
  {
    // Actual new data. Save!
    prefs.putUInt("FCntUp", newFCntUp);
    prefs.putUInt("FCntDown", newFCntDown);
  }
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

}

void loop()
{
  if (sendingDone)
  {
    LoRa_rxMode();
    sendingDone = false;
  }

  if (msgAvailable)
  {
    msgAvailable = false;

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

    loRaWAN.parseMessage(&msg[0], msgLen, LoRa.packetRssi(), firstMsg);
    firstMsg = false;

    Serial.println("Message parsed");
  }

  handleLights();
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
  loRaWAN.OTAAEnabled = false;
  memcpy(&loRaWAN.devAddr, devAddr, 4);
  memcpy(&loRaWAN.appSKey, appSKey, 16);
  memcpy(&loRaWAN.nwkSKey, nwkSKey, 16);

  // Load from persistent storage
  prevFCntUp = prefs.getUInt("FCntUp", 0);
  prevFCntDown = prefs.getUInt("FCntDown", 0);
  loRaWAN.fCntUp = prevFCntUp << 7;           // ignore the last 128 bits to limit flash wear.
  loRaWAN.fCntDown = (prevFCntDown + 1) << 7; // Also add extra so that we do not overlap frame counts.

  loRaWAN.onSave(LoRaWAN_onSave);
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
