#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

#define FASTLED_ALLOW_INTERRUPTS 0
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
status_t statusActive;
command_t commandBufferOut;
command_t commandBufferIn;

uint8_t pendingCommand = 0;
uint8_t pendingStatus = 0;
uint8_t pendingTransmission = 0;

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
#define WSPIN 3
// #define WSPIN D8
#define WSLEDS 100
#define WSMAXBRIGHT 88
#define LED_UPDATE_PERIOD 17
#define DEFAULT_SPEED 24
#define DEFAULT_WIDTH 1
#define DEFAULT_COUNT 2

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
// void FillLEDsFromPaletteColors(uint8_t colorIndex);

// ----- Program Switching -----

Ticker ledDisplayTicker;

void handleStatus();
void handleActive();
void handleSpeed();
void handleProgram();
void handleWidth();
void handleRefresh();
void handleMaxBright();
void handleCount();

void updateProgram();
void updateActive();
void updateSpeed();
void updateWidth();
void updateRefresh();
void updateMaxBright();
void updateCount();

void enablePrgBlack();
void disablePrgBlack();
void prgBlackDo();
Ticker prgBlackTicker;

void enablePrgWhite50();
void disablePrgWhite50();

void enablePrgRainbow();
void disablePrgRainbow();
void incrementPallete();
Ticker rainbowTicker;

void enablePrgTwinkle();
void disablePrgTwinkle();

void disableAllPrg();

uint8_t pendingPongOut = 0;
uint8_t isConnected = 0;
uint32_t remoteTimestamp = 0L;

Ticker ledTicker;
Ticker watchdogTicker;

CRGBArray<WSLEDS> leds;
CRGBPalette16 currentPalette;
TBlendType currentBlending;

uint8_t startIndex = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println();

  // Initialize status
  statusActive.active            = 0;
  statusActive.program           = WLEDC_PRG_BLACK;
  statusActive.speed             = DEFAULT_SPEED;
  statusActive.width             = DEFAULT_WIDTH;
  statusActive.refresh_period_ms = LED_UPDATE_PERIOD;
  statusActive.maxbright         = WSMAXBRIGHT;
  statusActive.count             = DEFAULT_COUNT;

  statusLocal.active             = 1;
  statusLocal.program            = WLEDC_PRG_RAINBOW;
  statusLocal.speed              = DEFAULT_SPEED;
  statusLocal.width              = DEFAULT_WIDTH;
  statusLocal.refresh_period_ms  = LED_UPDATE_PERIOD;
  statusLocal.maxbright          = WSMAXBRIGHT;
  statusLocal.count              = 2;
  pendingStatus = 1;

  InitWifi();

  Serial.print("This node AP mac: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("This node STA mac: ");
  Serial.println(WiFi.macAddress());

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);

  LEDS.addLeds<WS2812, WSPIN, GRB>(leds, WSLEDS);
  FastLED.setBrightness(statusActive.maxbright);
  FastLED.setCorrection(Typical8mmPixel);

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

}

void loop()
{

  handleCommand();
  handlePing();
  handleStatus();

  if (isConnected) {
    digitalWrite(STATUS_LED, HIGH);
  } else {
    digitalWrite(STATUS_LED, LOW);
  }
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
  pendingTransmission = 0;
  digitalWrite(LED_BUILTIN, LOW);
  ledTicker.once_ms_scheduled(80, flickLED);
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
  pendingTransmission = 1;
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
  if (pendingCommand) {

    remoteTimestamp = commandBufferIn.timestamp;

    switch (commandBufferIn.cmd) {
      case WLEDC_CMD_NULL:
        // Null means do nothing
        // Serial.println("NULL CMD");
        break;
      case WLEDC_CMD_OFF:
        // Serial.println("OFF CMD");
        statusLocal.active = 0;
        pendingStatus = 1;
        break;
      case WLEDC_CMD_GETSTATUS:
        // The status has been requested. We should send it.
        sendStatus();
        break;
      case WLEDC_CMD_SETSTATUS:
        // A new status has been sent. We should update our local copy.
        memcpy(&statusLocal, &(commandBufferIn.stat), sizeof(commandBufferIn.stat));
        pendingStatus = 1;
        break;
      case WLEDC_CMD_STATUS:
        // Not for us to receive
        break;
      case WLEDC_CMD_PING:
        pendingPongOut = 1;
        break;
      case WLEDC_CMD_PONG:
        // Not for us to receive
        break;
    };
    pendingCommand = 0;
  }
}

void handleStatus() {
  if (pendingStatus) {
    handleActive();
    handleProgram();
    handleSpeed();
    handleWidth();
    handleMaxBright();
    handleRefresh();
    handleCount();
    pendingStatus = 0;
  }
}

void handleActive() {
  if (statusActive.active != statusLocal.active) {
    statusActive.active = statusLocal.active;
    updateActive();
  }
}

void handleSpeed() {
  Serial.println("Updating Speed");
  if (statusActive.speed != statusLocal.speed) {
    statusActive.speed = statusLocal.speed;
    updateSpeed();
  }
}

void handleProgram() {
  if (statusActive.program != statusLocal.program) {
    statusActive.program = statusLocal.program;
    updateProgram();
  }
}

void handleWidth() {
  if (statusActive.width != statusLocal.width) {
    statusActive.width = statusLocal.width;
    updateWidth();
  }
}

void handleRefresh() {
  if (statusActive.refresh_period_ms != statusLocal.refresh_period_ms) {
    statusActive.refresh_period_ms = statusLocal.refresh_period_ms;
    updateRefresh();
  }
}

void handleMaxBright() {
  if (statusActive.maxbright != statusLocal.maxbright) {
    statusActive.maxbright = statusLocal.maxbright;
    updateMaxBright();
  }
}

void handleCount() {
  if (statusActive.count != statusLocal.count) {
    statusActive.count = statusLocal.count;
    updateCount();
  }
}

void updateActive() {
  Serial.println("Updating Active");
  if (statusActive.active) {
    ledDisplayTicker.detach();
    ledDisplayTicker.attach_ms_scheduled(statusActive.refresh_period_ms, drawLEDs);
  } else {
    ledDisplayTicker.detach();
    leds.fill_solid(CRGB::Black);
    FastLED.show();
  }
}

void updateProgram() {
  Serial.println("Updating Program");
  disableAllPrg();
  switch (statusActive.program) {
    case WLEDC_PRG_BLACK:
      enablePrgBlack();
      break;
    case WLEDC_PRG_WHITE50:
      enablePrgWhite50();
      break;
    case WLEDC_PRG_RAINBOW:
      enablePrgRainbow();
      break;
    case WLEDC_PRG_TWINKLE:
      enablePrgTwinkle();
      break;
  };
}

void updateSpeed() {
  Serial.println("Updating Speed");
  // Find active program and reset
  switch (statusActive.program) {
    case WLEDC_PRG_BLACK:
      break;
    case WLEDC_PRG_WHITE50:
      disablePrgWhite50();
      enablePrgWhite50();
      break;
    case WLEDC_PRG_RAINBOW:
      disablePrgRainbow();
      enablePrgRainbow();
      break;
    case WLEDC_PRG_TWINKLE:
      disablePrgTwinkle();
      enablePrgTwinkle();
      break;
  }
}

void updateWidth() {
  Serial.println("Updating Width");
}

void updateRefresh() {
  Serial.println("Updating Refresh Period");
  if (ledDisplayTicker.active()) {
    ledDisplayTicker.detach();
    ledDisplayTicker.attach_ms_scheduled(statusActive.refresh_period_ms, drawLEDs);
  }
}

void updateMaxBright() {
  Serial.print("Updating Maximum Bright Level: ");
  Serial.println(statusActive.maxbright);
  FastLED.setBrightness(statusActive.maxbright);
}

void updateCount() {
  Serial.println("Updating Count");
  Serial.println(statusActive.count);
}

//-------------
// LED PATTERNS
//-------------

void drawLEDs() {
  if (!pendingTransmission) {
    FastLED.show();
  }
}

void incrementPallete() {
  static uint8_t currentHue;
  currentHue += statusActive.width;
  // leds.fill_rainbow(currentHue);
  leds.fill_rainbow(currentHue, (uint8_t)statusActive.count);
  // leds.fill_rainbow(currentHue, 1);
  // Serial.println(statusActive.count);
}

// ----- Program Switching -----
// ----- BLACK (Off) -----
void enablePrgBlack() {
  Serial.println("Enable Program Black");
  prgBlackTicker.attach_ms_scheduled(1000, prgBlackDo);
}

void disablePrgBlack() {
  Serial.println("Disable Program Black");
  prgBlackTicker.detach();
}

void prgBlackDo() {
  leds.fill_solid(CRGB::Black);
}

// ----- White 50% -----
void enablePrgWhite50() {
  Serial.println("Enable Program White50");
}

void disablePrgWhite50() {
  Serial.println("Disable Program White50");
}

// ----- Rainbow -----
void enablePrgRainbow() {
  Serial.println("Enable Program Rainbow");
  rainbowTicker.attach_ms_scheduled(statusActive.speed, incrementPallete);
}

void disablePrgRainbow() {
  Serial.println("Disable Program Rainbow");
  rainbowTicker.detach();
}

// ----- Twinkly -----
void enablePrgTwinkle() {
  Serial.println("Enable Program Twinkle");
}

void disablePrgTwinkle() {
  Serial.println("Disable Program Twinkle");
}

// ----- Disable All -----
void disableAllPrg() {
  Serial.println("Disable Program All");

  disablePrgBlack();
  disablePrgWhite50();
  disablePrgRainbow();
  disablePrgTwinkle();

  // enablePrgBlack();
}
