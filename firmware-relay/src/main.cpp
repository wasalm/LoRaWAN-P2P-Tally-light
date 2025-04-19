// Network
#include <ESP8266WiFi.h>
#include <espnow.h>

// Board pins
#define RELAY_PIN 12
#define LED_PIN 13
#define BUTTON_PIN 0

// MAC Address
uint8_t broadcastAddress[] = {0xF4, 0xCF, 0xA2, 0x16, 0x47, 0x4D};

// Structure of the data send over the wifi.
typedef struct struct_message
{
  unsigned int batteryVoltage;
  bool doorOpened;
} struct_message;

// Create a struct_message to store data
struct_message message;
bool newDataAvailable = false;

/*
 *  Setup scripts
 */
void setupPins()
{
  pinMode(BUTTON_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT); // LED is inverted

  digitalWrite(LED_PIN, HIGH);
  digitalWrite(RELAY_PIN, LOW);
}

void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len);
void setupWifi() {
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  wifi_set_macaddr(STATION_IF, &broadcastAddress[0]);

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info4
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(OnDataRecv);
}

void setup()
{
  Serial.begin(115200);
  setupPins();
  setupWifi();
}

/*
 * Relay actions
 */

void powerOn()
{
  digitalWrite(RELAY_PIN, HIGH);
}

void powerOff()
{
  digitalWrite(RELAY_PIN, LOW);
}

void blink()
{
  for (int i = 0; i < 3; i++)
  {
    powerOn();
    delay(1000);
    powerOff();
    delay(1000);
  }
}

/*
 * Loop
 */

void loopLocalButton()
{
  static bool prevState = false;
  static bool actionTook = false;
  static unsigned long lastChanged = 0;

  bool state = digitalRead(BUTTON_PIN);

  if (state != prevState)
  {
    prevState = state;
    actionTook = false;
    lastChanged = millis();
  }
  else
  {
    if (state == LOW && !actionTook && millis() - lastChanged > 100)
    {
      blink();
      actionTook = true;
    }
  }
}

void loopRemoteData()
{
  if (newDataAvailable)
  {
    if (message.doorOpened)
    {
      blink();
    }
    newDataAvailable = false;
  }
}

void loop()
{
  loopLocalButton();
  loopRemoteData();
}

/*
 *
 */
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  memcpy(&message, incomingData, sizeof(message));
  newDataAvailable = true;
}