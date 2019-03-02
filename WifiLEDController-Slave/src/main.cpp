#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

// #define FASTLED_ALLOW_INTERRUPTS 0
// #define FASTLED_INTERRUPT_RETRY_COUNT 1
#define FASTLED_ESP8266_DMA
#include <FastLED.h>

extern "C"
{
#include <espnow.h>
#include "user_interface.h"
}

#include <../../common/wifiledcontroller.h>

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
#define STATUS_LED D1
#define WSPIN D8
#define WSLEDS 8
#define WSMAXBRIGHT 16
#define LED_UPDATE_PERIOD 17

void printMacAddress(uint8_t* macaddr);
void onDataSent(uint8_t* macaddr, uint8_t status);
void onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len);
void InitWifi();
void InitESPNow();

void sendStatus();
void sendCommand(command_type_t command);

void flickLED();

void handleCommand();
void handlePing();

void watchdogReset();
void watchdogExpire();

void drawLEDs();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void incremenPallete();

// ----- Program Switching -----
void enablePrgBlack();
void disablePrgBlack();
void prgBlackDo();
Ticker prgBlackTicker;

void enablePrgWhite50();
void disablePrgWhite50();

void enablePrgRainbow();
void disablePrgRainbow();

void enablePrgTwinkle();
void disablePrgTwinkle();

void disableAllPrg();

uint8_t pendingPongOut = 0;
uint8_t isConnected = 0;

Ticker ledTicker;
Ticker watchdogTicker;

Ticker ledDisplayTicker;

Ticker rainbowTicker;

CRGBArray<WSLEDS> leds;
CRGBPalette16 currentPalette;
TBlendType currentBlending;

uint8_t startIndex = 0;

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
  pinMode(STATUS_LED, OUTPUT);

  LEDS.addLeds<WS2812, WSPIN, GRB>(leds, WSLEDS);
  FastLED.setBrightness(WSMAXBRIGHT);

  currentPalette = RainbowColors_p;
  currentBlending = LINEARBLEND;

  int addStatus = esp_now_add_peer((u8*)remoteMac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  if (addStatus == 0) {
    // Pair success
    Serial.println("Pair success");
    // digitalWrite(STATUS_LED, HIGH);
  } else {
    Serial.println("Pair failed");
    // digitalWrite(STATUS_LED, LOW);
  }

  ledDisplayTicker.attach_ms_scheduled(LED_UPDATE_PERIOD, drawLEDs);

  rainbowTicker.attach_ms_scheduled(15, incremenPallete);
}

void loop()
{

  handleCommand();
  handlePing();

  statusLocal.timestamp = millis();

  if (isConnected) {
    digitalWrite(STATUS_LED, HIGH);
  } else {
    digitalWrite(STATUS_LED, LOW);
  }

  // FastLED.show();
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
      pendingCommand = 1;
    }
    digitalWrite(LED_BUILTIN, LOW);
    ledTicker.once_ms_scheduled(8, flickLED);
  }
  // watchdogReset();
}

void flickLED() {
  // ledTicker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void sendStatus() {
  // Serial.println("Sending requested Status");
  sendCommand(WLEDC_CMD_STATUS);
}

void sendCommand(command_type_t command) {
  commandBufferOut.cmd=command;
  memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));
  esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
}

void handlePing() {
  if (pendingPongOut) {
    pendingPongOut = 0;
    // Reset watchdog timer
    watchdogReset();
    // Send Pong!
    sendCommand(WLEDC_CMD_PONG);
    // Serial.println("Sending Pong");
  }
}

void watchdogExpire() {
  // Serial.println("Watchdog Expired...");
  isConnected = 0;
}

void watchdogReset() {
  watchdogTicker.detach();
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
      statusLocal.active = 0;
      break;
    case WLEDC_CMD_GETSTATUS:
      // The status has been requested. We should send it.
      // Serial.println("GETSTATUS CMD");
      sendStatus();
      break;
    case WLEDC_CMD_SETSTATUS:
      // A new status has been sent. We should update our local copy.
      // Serial.println("SETSTATUS CMD");
      memcpy(&statusLocal, &(commandBufferIn.stat), sizeof(commandBufferIn.stat));
      // Serial.print("  active: ");
      // Serial.println(statusLocal.active);
      break;
    case WLEDC_CMD_STATUS:
      // Not for us to receive
      break;
    case WLEDC_CMD_PING:
      // Serial.println("PING!");
      pendingPongOut = 1;
      break;
    case WLEDC_CMD_PONG:
      // Not for us to receive
      break;
    };
    pendingCommand = 0;
  }
}

//-------------
// LED PATTERNS
//-------------

void drawLEDs() {
  FastLED.show();
}

void FillLEDsFromPaletteColors(uint8_t colorIndex)
{
  uint8_t brightness = 255;

  for (int i = 0; i < WSLEDS; i++)
  { 
    leds[i] = ColorFromPalette(currentPalette, colorIndex, brightness, currentBlending);
    colorIndex += 12;
  }
}

void incremenPallete() {
  startIndex++; /* motion speed */
  FillLEDsFromPaletteColors(startIndex);
}

// ----- Program Switching -----
// ----- BLACK (Off) -----
void enablePrgBlack() {
  prgBlackTicker.attach_ms_scheduled(1000, prgBlackDo);
}

void disablePrgBlack() {
  prgBlackTicker.detach();
}

void prgBlackDo() {
  leds.fill_solid(CRGB::Black);
}

// ----- White 50% -----
void enablePrgWhite50() {

}

void disablePrgWhite50() {

}

// ----- Rainbow -----
void enablePrgRainbow() {

}

void disablePrgRainbow() {

}

// ----- Twinkly -----
void enablePrgTwinkle() {

}

void disablePrgTwinkle() {

}

// ----- Disable All -----
void disableAllPrg() {
  disablePrgBlack();
  disablePrgWhite50();
  disablePrgRainbow();
  disablePrgTwinkle();

  enablePrgBlack();
}
