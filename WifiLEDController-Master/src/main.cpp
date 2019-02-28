#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

extern "C"
{
#include <espnow.h>
#include <user_interface.h>
}

#include "../../common/wifiledcontroller.h"

status_t statusLocal;
status_t statusRemote;
command_t commandBufferOut;
command_t commandBufferIn;

typedef struct esp_now_peer_info {
    u8 peer_addr[6];    /**< ESPNOW peer MAC address that is also the MAC address of station or softap */
    uint8_t channel;    /**< Wi-Fi channel that peer uses to send/receive ESPNOW data. If the value is 0,
                             use the current channel which station or softap is on. Otherwise, it must be
                              set as the channel that station or softap is on. */
} esp_now_peer_info_t;

// this is the MAC Address of the remote ESP server which receives these sensor readings
uint8_t remoteMac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x33};
uint8_t mac[]       = {0x36, 0x33, 0x33, 0x33, 0x33, 0x35};

#define CHANNEL 1
#define STATUS_LED D1

void printMacAddress(uint8_t* macaddr);
void onDataSent(uint8_t* macaddr, uint8_t status);
void onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len);
void InitWifi();
void InitESPNow();
void requestStatus();
void flickLED();
void toggleActiveAndSend();

void handleCommand();
void handlePong();

void sendStatus();
void sendCommand(command_type_t command);
void sendPing();

void watchdogReset();
void watchdogExpire();

bool retry = true;

uint8_t pendingStatus = 0;
uint8_t pendingCommand = 0;
uint8_t isConnected = 0;
uint8_t pendingPongIn = 0;

Ticker reqStatusTicker;
Ticker ledTicker;
Ticker toggleActiveTicker;
Ticker watchdogTicker;
Ticker pingTicker;

void setup()
{
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  Serial.println();

  InitWifi();

  Serial.print("This node AP mac: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("This node STA mac: ");
  Serial.println(WiFi.macAddress());

  int addStatus = esp_now_add_peer((u8*)remoteMac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  if (addStatus == 0) {
    // Pair success
    Serial.println("Pair success");
    // digitalWrite(STATUS_LED, HIGH);
  } else {
    Serial.println("Pair failed");
    // digitalWrite(STATUS_LED, LOW);
  }

  // reqStatusTicker.attach_ms(2000, requestStatus);
  // toggleActiveTicker.attach_ms(876, toggleActiveAndSend);
  pingTicker.attach_ms_scheduled(1000, sendPing);
}

void loop()
{
  handleCommand();
  handlePong();

  // if (pendingStatus) {
  //   Serial.println("Incoming status:");
  //   memcpy(&statusLocal, &(commandBufferIn.stat), sizeof(statusLocal));
  //   Serial.print("  active: ");
  //   Serial.println(statusLocal.active);
  //   Serial.print("  program: ");
  //   Serial.println(statusLocal.program);
  //   Serial.print("  speed: ");
  //   Serial.println(statusLocal.speed);
  //   Serial.print("  refresh_period_ms: ");
  //   Serial.println(statusLocal.refresh_period_ms);
  //   Serial.print("  timestamp: ");
  //   Serial.println(statusLocal.timestamp);
  //   pendingStatus = 0;
  // }

  if (isConnected) {
    digitalWrite(STATUS_LED, HIGH);
  } else {
    digitalWrite(STATUS_LED, LOW);
  }

}

void printMacAddress(uint8_t* macaddr) {
  Serial.print("{");
  for (int i = 0; i < 6; i++) {
    Serial.print("0x");
    Serial.print(macaddr[i], HEX);
    if (i < 5) Serial.print(',');
  }
  Serial.println("};");
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
  ledTicker.once_ms_scheduled(80, flickLED);
  // Serial.println("Data sent successfully");
}

void onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len) {
  // The only data we will actually be receiving is a command packet with the remote's status
  // Only act if that's the case
  // Serial.println("Command Received");
  if (!pendingCommand) {
    if (len == sizeof(commandBufferIn)) {
      memcpy(&commandBufferIn, data, sizeof(commandBufferIn));
    }
    pendingCommand = 1;
  }

  digitalWrite(LED_BUILTIN, LOW);
  ledTicker.once_ms_scheduled(8, flickLED);
}

void requestStatus() {
  // Serial.println("Requesting Status");
  commandBufferOut.cmd=WLEDC_CMD_GETSTATUS;
  memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));
  esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
}

void flickLED() {
  // ledTicker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void toggleActiveAndSend() {
  statusLocal.active = !(statusLocal.active);
  sendStatus();
}

void sendStatus() {
  // Serial.println("Sending Status");
  sendCommand(WLEDC_CMD_SETSTATUS);
}

void sendCommand(command_type_t command) {
  // Serial.println("Sending requested Status");
  commandBufferOut.cmd=command;
  memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));
  esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
}

void sendPing() {
  sendCommand(WLEDC_CMD_PING);
  // Serial.println("Sending Ping");
  // pendingPongIn = 1;
}

void handlePong() {
  if (pendingPongIn) {
    pendingPongIn = 0;
    // Reset watchdog timer
    watchdogReset();
    // Serial.println("Got Pong");
  }
}

void watchdogExpire() {
  // Serial.println("Watchdog Expired...");
  isConnected = 0;
}

void watchdogReset() {
  // watchdogTicker.detach();
  isConnected = 1;
  watchdogTicker.once_ms_scheduled(1200, watchdogExpire);
}

void handleCommand() {
  if (pendingCommand)
  {
    switch (commandBufferIn.cmd)
    {
    case WLEDC_CMD_NULL:
      // Null means do nothing
      // Serial.println("NULL CMD");
      break;
    case WLEDC_CMD_OFF:
      // Serial.println("OFF CMD");
      break;
    case WLEDC_CMD_GETSTATUS:
      // The status has been requested. We should send it.
      // Serial.println("GETSTATUS CMD");
      break;
    case WLEDC_CMD_SETSTATUS:
      // A new status has been sent. We should update our local copy.
      // Serial.println("SETSTATUS CMD");
      break;
    case WLEDC_CMD_STATUS:
      break;
    case WLEDC_CMD_PING:
      // Server doesn't get PINGed
      break;
    case WLEDC_CMD_PONG:
      // Serial.println("PONG!");
      pendingPongIn = 1;
      break;
    };

    // Copy status that was sent by the client
    memcpy(&(commandBufferIn.stat), &statusRemote, sizeof(statusRemote));

    pendingCommand = 0;
  }
}