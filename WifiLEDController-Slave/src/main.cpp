#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

extern "C"
{
#include <espnow.h>
#include "user_interface.h"
}

enum command_type_t : uint8_t {WLEDC_CMD_NULL, WLEDC_CMD_OFF, WLEDC_CMD_GETSTATUS, WLEDC_CMD_SETSTATUS, WLEDC_CMD_STATUS};

struct __attribute__((packed)) status_t
{
  uint8_t active;
  uint16_t program;
  uint16_t speed;
  uint16_t refresh_period_ms;
  uint32_t timestamp;
};

struct __attribute__((packed)) command_t
{
  command_type_t cmd;
  status_t stat;
};

status_t statusLocal;
command_t commandBufferOut;
command_t commandBufferIn;

uint8_t pendingCommand = 0;

/* Set a private Mac Address
 *  http://serverfault.com/questions/40712/what-range-of-mac-addresses-can-i-safely-use-for-my-virtual-machines
 * Note: the point of setting a specific MAC is so you can replace this Gateway ESP8266 device with a new one
 * and the new gateway will still pick up the remote sensors which are still sending to the old MAC 
 */
uint8_t mac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x33};
uint8_t remoteMac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x35};


void initVariant()
{
  WiFi.mode(WIFI_AP);
  wifi_set_macaddr(SOFTAP_IF, &mac[0]);
}

#define CHANNEL 1

void printMacAddress(uint8_t* macaddr);
void onDataSent(uint8_t* macaddr, uint8_t status);
void onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len);
void InitWifi();
void InitESPNow();
void sendStatus();
void flickLED();

Ticker ledTicker;

void setup()
{
  Serial.begin(115200);
  Serial.println();

  // initVariant();
  InitWifi();

  Serial.print("This node AP mac: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("This node STA mac: ");
  Serial.println(WiFi.macAddress());

  pinMode(LED_BUILTIN, OUTPUT);

  int addStatus = esp_now_add_peer((u8*)remoteMac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  if (addStatus == 0) {
    // Pair success
    Serial.println("Pair success");
    // digitalWrite(STATUS_LED, HIGH);
  } else {
    Serial.println("Pair failed");
    // digitalWrite(STATUS_LED, LOW);
  }

}

void loop()
{
  // {WLEDC_CMD_NULL, WLEDC_CMD_OFF, WLEDC_CMD_GETSTATUS, WLEDC_CMD_SETSTATUS, WLEDC_CMD_STATUS}
  if (pendingCommand) {
    switch (commandBufferIn.cmd) {
      case WLEDC_CMD_NULL:
        // Null means do nothing
        Serial.println("NULL CMD");
        break;
      case WLEDC_CMD_OFF:
        Serial.println("OFF CMD");
        statusLocal.active = 0;
        break;
      case WLEDC_CMD_GETSTATUS:
        // The status has been requested. We should send it.
        Serial.println("GETSTATUS CMD");
        sendStatus();
        break;
      case WLEDC_CMD_SETSTATUS:
        // A new status has been sent. We should update our local copy.
        Serial.println("SETSTATUS CMD");
        memcpy(&statusLocal, &(commandBufferIn.stat), sizeof(commandBufferIn.stat));
        Serial.print("  active: ");
        Serial.println(statusLocal.active);
        break;
      case WLEDC_CMD_STATUS:
        Serial.println("STATUS CMD");
        // Used for client to return requested status. Since only client sends, client should ignore this one.
        break;
    };
    pendingCommand = 0;
  }

  statusLocal.timestamp = millis();
}

void InitWifi() {
  // WiFi.mode(WIFI_STA);
  // wifi_set_macaddr(STATION_IF, mac);
  WiFi.mode(WIFI_AP);
  wifi_set_macaddr(SOFTAP_IF, &mac[0]);

  InitESPNow();
}

void InitESPNow() {
  if (esp_now_init() == 0) {
    Serial.println("ESPNow Init Success");
  }
  else {
    Serial.println("ESPNow Init Failed");
    ESP.restart();
  }
  // esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
}

void onDataSent(uint8_t* macaddr, uint8_t status) {
  digitalWrite(LED_BUILTIN, LOW);
  ledTicker.attach_ms(80, flickLED);
  Serial.println("Data sent successfully");
}

void onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len) {
  // The only data we will actually be receiving is a command packet with the remote's status
  // Only act if that's the case
  Serial.println("Command Received");
  if (len == sizeof(commandBufferIn)) {
    memcpy(&commandBufferIn, data, sizeof(commandBufferIn));
    pendingCommand = 1;
  }
  digitalWrite(LED_BUILTIN, LOW);
  ledTicker.attach_ms(8, flickLED);
}

void flickLED() {
  ledTicker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void sendStatus() {
  Serial.println("Sending requested Status");
  commandBufferOut.cmd=WLEDC_CMD_STATUS;
  memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));
  int result = esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
}

