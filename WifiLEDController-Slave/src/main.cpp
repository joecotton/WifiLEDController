
#include <Arduino.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>

#define FASTLED_ALLOW_INTERRUPTS 0
// #define FASTLED_INTERRUPT_RETRY_COUNT 1
#define FASTLED_ESP8266_DMA
#include <FastLED.h>

extern "C"
{
#include <espnow.h>
#include "user_interface.h"
}

#include "../../common/wifiledcontroller.h"

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
uint8_t mac[] = {0x36,  0x33, 0x33, 0x33, 0x33, 0x33};
uint8_t remoteMac[] = {0x36, 0x33, 0x33, 0x33, 0x33, 0x35};

void initVariant()
{
  WiFi.mode(WIFI_AP);
  wifi_set_macaddr(SOFTAP_IF, &mac[0]);
}

#define CHANNEL 1
#define STATUS_LED D1
#define WSPIN 3  // RX
// #define WSPIN D5
#define WSLEDS 500
#define MAX_POWER_VOLTS 12
#define MAX_POWER_MA 8000
#define MIN_SPEED_ALT ((uint16_t)15)

#define THROTTLE_DELAY 10U

#undef max
#define max(a,b) ((a)>(b)?(a):(b))

void printMacAddress(uint8_t* macaddr);
void ICACHE_RAM_ATTR onDataSent(uint8_t *macaddr, uint8_t status);
void ICACHE_RAM_ATTR onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len);
void InitWifi();
void InitESPNow();

void sendStatus();
void sendCommand(command_type_t command);
uint8_t volatile Throttle = 0;

void ICACHE_RAM_ATTR flickLED();

void ICACHE_RAM_ATTR handleCommand();
void ICACHE_RAM_ATTR handlePing();

void printState(status_t state);

void ICACHE_RAM_ATTR watchdogReset();
void ICACHE_RAM_ATTR watchdogExpire();
void commandThrottle();

void drawLEDs();
void drawNothing();

// ----- Program Switching -----

Ticker ledDisplayTicker;

// Set flags for changes
void handleStatus();
void handleActive();
void handleSpeed();
void ICACHE_RAM_ATTR handleProgram();
void handleWidth();
void handleHue();
void handleSaturation();
void handleRefresh();
void handleMaxBright();
void handleCount();

// Program: Black
void enablePrgBlack();
void disablePrgBlack();
void ICACHE_RAM_ATTR prgBlackDo();
Ticker prgBlackTicker;

// Program: White
void enablePrgWhite50();
void disablePrgWhite50();
void ICACHE_RAM_ATTR prgWhite50Do();
Ticker prgWhite50Ticker;

// Program: Rainbow
void enablePrgRainbow();
void disablePrgRainbow();
void ICACHE_RAM_ATTR incrementPallete();
Ticker rainbowTicker;

// Program: Twinkle
void enablePrgTwinkle();
void disablePrgTwinkle();
void ICACHE_RAM_ATTR makeTwinkle();
void ICACHE_RAM_ATTR twinkleFade();
Ticker twinkleTicker;
Ticker twinkleFadeTicker;

// Program: Waves
void enablePrgWaves();
void disablePrgWaves();
void ICACHE_RAM_ATTR waveDraw();
Ticker waveTimer;

// Program: Waves 2
void enablePrgWaves2();
void disablePrgWaves2();
void ICACHE_RAM_ATTR wave2Draw();
Ticker wave2Timer;

// Program: Dots
void enablePrgDots();
void disablePrgDots();
void ICACHE_RAM_ATTR dotDraw();
Ticker dotTimer;

// Program: Twinkle Rainbow
void enablePrgTwinkleR();
void disablePrgTwinkleR();
void ICACHE_RAM_ATTR makeTwinkleR();
void ICACHE_RAM_ATTR twinkleRFade();
Ticker twinkleRTicker;
Ticker twinkleRFadeTicker;

void ICACHE_RAM_ATTR disableAllPrg();

Ticker memReportTicker;
void printMem();

uint8_t pendingPongOut = 0;
uint8_t isConnected = 0;
// uint32_t remoteTimestamp = 0L;

Ticker ledTicker;
Ticker watchdogTicker;
Ticker commandThrottleTicker;

CRGBArray<WSLEDS> leds;
CRGBPalette16 currentPalette;
TBlendType currentBlending;

uint8_t startIndex = 0;

void setup()
{
  Serial.begin(115200);
  Serial.println();

  // memReportTicker.attach_ms(500U, printMem);

  // Initialize status
  statusActive.active            = DEFAULT_ACTIVE;
  statusActive.program           = DEFAULT_PROGRAM;
  statusActive.speed             = DEFAULT_SPEED;
  statusActive.width             = DEFAULT_WIDTH;
  statusActive.hue               = DEFAULT_HUE;
  statusActive.saturation        = DEFAULT_SATURATION;
  statusActive.refresh_period_ms = DEFAULT_REFRESH;
  statusActive.maxbright         = DEFAULT_BRIGHT;
  statusActive.step              = DEFAULT_STEP;

  statusLocal.active             = DEFAULT_ACTIVE;
  statusLocal.program            = DEFAULT_PROGRAM;
  statusLocal.speed              = DEFAULT_SPEED;
  statusLocal.width              = DEFAULT_WIDTH;
  statusLocal.hue                = DEFAULT_HUE;
  statusLocal.saturation         = DEFAULT_SATURATION;
  statusLocal.refresh_period_ms  = DEFAULT_REFRESH;
  statusLocal.maxbright          = DEFAULT_BRIGHT;
  statusLocal.step               = DEFAULT_STEP;

  InitWifi();

  Serial.print("This node AP mac: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("This node STA mac: ");
  Serial.println(WiFi.macAddress());

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);

  FastLED.addLeds<WS2812, WSPIN, GRB>(leds, WSLEDS);
  // FastLED.setMaxPowerInVoltsAndMilliamps(MAX_POWER_VOLTS, MAX_POWER_MA);
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
    // digitalWrite(STATUS_LED, HIGH);
    analogWrite(STATUS_LED, 0x0008);
  } else {
    // digitalWrite(STATUS_LED, LOW);
    analogWrite(STATUS_LED, 0x0000);
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

void ICACHE_RAM_ATTR onDataSent(uint8_t *macaddr, uint8_t status) {
  pendingTransmission = 0;
  digitalWrite(LED_BUILTIN, LOW);
  ledTicker.once_ms(10, flickLED);
}

void ICACHE_RAM_ATTR onDataRecv(uint8_t *macaddr, uint8_t *data, uint8_t len) {
  // The only data we will actually be receiving is a command packet with the remote's status
  // Only act if that's the case
  // Serial.println("Command Received");
  if (!pendingCommand && !Throttle) {
    if (len == sizeof(commandBufferIn)) {
      memcpy(&commandBufferIn, data, sizeof(commandBufferIn));
      pendingCommand = 1;
    }
    digitalWrite(LED_BUILTIN, LOW);
    ledTicker.once_ms(8, flickLED);
    // watchdogReset();
    commandThrottleTicker.once_ms(THROTTLE_DELAY, commandThrottle);
  }
}

void ICACHE_RAM_ATTR flickLED() {
  // ledTicker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void commandThrottle() { Throttle = 0; }

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

void ICACHE_RAM_ATTR handlePing() {
  if (pendingPongOut) {
    pendingPongOut = 0;
    // Reset watchdog timer
    watchdogReset();
    // Send Pong!
    sendCommand(WLEDC_CMD_PONG);
    // Serial.println("Sending Pong");
  }
}

void ICACHE_RAM_ATTR watchdogExpire() {
  // Serial.println("Watchdog Expired...");
  isConnected = 0;
}

void ICACHE_RAM_ATTR watchdogReset() {
  watchdogTicker.detach();
  isConnected = 1;
  watchdogTicker.once_ms(2000, watchdogExpire);
}

void ICACHE_RAM_ATTR handleCommand() {
  if (pendingCommand) {

    // remoteTimestamp = commandBufferIn.timestamp;

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

    // Serial.println("handleCommand/local");
    // printState(statusLocal);
    // Serial.println("handleCommand/active");
    // printState(statusActive);

    pendingCommand = 0;
    // watchdogReset();
  }
}

void handleStatus() {
  if (pendingStatus) {
    handleActive();
    handleProgram();
    handleSpeed();
    handleWidth();
    handleHue();
    handleSaturation();
    handleMaxBright();
    handleRefresh();
    handleCount();
    pendingStatus = 0;
  }
}

void handleActive() {
  Serial.println("Handling Active");
  if (statusActive.active != statusLocal.active) {
    statusActive.active = statusLocal.active;
    Serial.println("Updating Active");
    if (statusActive.active) {
      ledDisplayTicker.detach();
      ledDisplayTicker.attach_ms(statusActive.refresh_period_ms, drawLEDs);
    } else {
      ledDisplayTicker.detach();
      ledDisplayTicker.attach_ms(statusActive.refresh_period_ms, drawNothing);
    }
  }
}

void handleSpeed() {
  Serial.println("Handling Speed");
  if (statusActive.speed != statusLocal.speed) {
    statusActive.speed = statusLocal.speed;
    Serial.println("Updating Speed");
    // Find active program and reset
    switch (statusActive.program) {
      case program_t::Black:
        break;
      case program_t::White:
        disablePrgWhite50();
        enablePrgWhite50();
        break;
      case program_t::Rainbow:
        disablePrgRainbow();
        enablePrgRainbow();
        break;
      case program_t::Twinkle:
        disablePrgTwinkle();
        enablePrgTwinkle();
        break;
      case program_t::Waves:
        disablePrgWaves();
        enablePrgWaves();
        break;
      case program_t::Dots:
        disablePrgDots();
        enablePrgDots();
        break;
      case program_t::TwinkleRainbow:
        disablePrgTwinkleR();
        enablePrgTwinkleR();
        break;
      case program_t::Waves2:
        disablePrgWaves2();
        enablePrgWaves2();
        break;
      case program_t::END_OF_LIST:
        break;
    }
  }
}

void ICACHE_RAM_ATTR handleProgram() {
  // Serial.println("Handling Program");
  if (statusActive.program != statusLocal.program) {
    statusActive.program = statusLocal.program;
    // Serial.println("Updating Program");
    disableAllPrg();
    // FastLED.clear();
    switch (statusActive.program) {
      case program_t::Black:
        enablePrgBlack();
        break;
      case program_t::White:
        enablePrgWhite50();
        break;
      case program_t::Rainbow:
        enablePrgRainbow();
        break;
      case program_t::Twinkle:
        enablePrgTwinkle();
        break;
      case program_t::Waves:
        enablePrgWaves();
        break;
      case program_t::Dots:
        enablePrgDots();
        break;
      case program_t::Waves2:
        enablePrgWaves2();
        break;
      case program_t::TwinkleRainbow:
        enablePrgTwinkleR();
        break;
      case program_t::END_OF_LIST:
        break;
    };
  }
  handleActive();
}

void handleWidth() {
  Serial.println("Handling Width");
  if (statusActive.width != statusLocal.width) {
    statusActive.width = statusLocal.width;
  }
}

void handleHue() {
  Serial.println("Handling Hue");
  if (statusActive.hue != statusLocal.hue) {
    statusActive.hue = statusLocal.hue;
  }
}

void handleSaturation() {
  Serial.println("Handling Saturation");
  if (statusActive.saturation != statusLocal.saturation) {
    statusActive.saturation = statusLocal.saturation;
  }
}

void handleRefresh() {
  Serial.println("Handling Refresh");
  if (statusActive.refresh_period_ms != statusLocal.refresh_period_ms) {
    statusActive.refresh_period_ms = min(max(statusLocal.refresh_period_ms,WLEDC_MIN_REFRESH), WLEDC_MAX_REFRESH);
    // if (statusLocal.refresh_period_ms < WLEDC_MIN_REFRESH) {
    //   statusActive.refresh_period_ms = WLEDC_MIN_REFRESH;
    // } else if (statusLocal.refresh_period_ms > WLEDC_MAX_REFRESH) {
    //   statusActive.refresh_period_ms = WLEDC_MAX_REFRESH;
    // } else {
    //   statusActive.refresh_period_ms = statusLocal.refresh_period_ms;
    // }
    Serial.println("Updating Refresh Period");
    if (ledDisplayTicker.active()) {
      ledDisplayTicker.detach();
      ledDisplayTicker.attach_ms( statusActive.refresh_period_ms, drawLEDs) ;
    } else {
      FastLED.clear();
      ledDisplayTicker.attach_ms(statusActive.refresh_period_ms, drawLEDs);
    }
  }
}

void handleMaxBright() {
  Serial.println("Handling Brightness");
  if (statusActive.maxbright != statusLocal.maxbright) {
    statusActive.maxbright = statusLocal.maxbright;
    Serial.print("Updating Maximum Bright Level: ");
    Serial.println(statusActive.maxbright);
    FastLED.setBrightness(statusActive.maxbright);
  }
}

void handleCount() {
  Serial.println("Handling Count");
  if (statusActive.step != statusLocal.step) {
    statusActive.step = statusLocal.step;
  }
}

//-------------
// LED PATTERNS
//-------------

void drawLEDs() {
  FastLED.show();
}

void drawNothing() {
  leds.fill_solid(CRGB::Black);
  FastLED.show();
}

void ICACHE_RAM_ATTR incrementPallete() {
  static uint8_t currentHue;
  currentHue += statusActive.width;
  // leds.fill_rainbow(currentHue);
  leds.fill_rainbow(currentHue, (uint8_t)statusActive.step);
  // leds.fill_rainbow(currentHue, 1);
  // Serial.println(statusActive.step);
}

// ----- Program Switching -----
// ----- BLACK (Off) -----
void enablePrgBlack() {
  Serial.println("Enable Program Black");
  prgBlackDo();
  prgBlackTicker.attach_ms(1000, prgBlackDo);
}

void disablePrgBlack() {
  Serial.println("Disable Program Black");
  prgBlackTicker.detach();
}

void ICACHE_RAM_ATTR prgBlackDo() {
  leds.fill_solid(CRGB::Black);
  // FastLED.show();
}

// ----- White 50% -----
void enablePrgWhite50() {
  Serial.println("Enable Program White");
  prgWhite50Do();
  prgWhite50Ticker.attach_ms(1000, prgWhite50Do);
}

void disablePrgWhite50() {
  Serial.println("Disable Program White");
  prgWhite50Ticker.detach();
}

void ICACHE_RAM_ATTR prgWhite50Do() {
  leds.fill_solid(CRGB::White);
}

// ----- Rainbow -----
void enablePrgRainbow() {
  Serial.println("Enable Program Rainbow");
  rainbowTicker.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    incrementPallete);
}

void disablePrgRainbow() {
  Serial.println("Disable Program Rainbow");
  rainbowTicker.detach();
}

// ----- Twinkle -----
void enablePrgTwinkle() {
  Serial.println("Enable Program Twinkle");
  random16_set_seed((uint16_t)ESP.getCycleCount());

  twinkleTicker.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    makeTwinkle);

  twinkleFadeTicker.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    twinkleFade);
}

void disablePrgTwinkle() {
  Serial.println("Disable Program Twinkle");
  twinkleTicker.detach();
  twinkleFadeTicker.detach();
}

void ICACHE_RAM_ATTR makeTwinkle() {
  // Iterate through all LEDS, choose random one to twinkle.
  for (uint16_t a=0; a<WSLEDS; a++) {
    if (random16(WLEDC_MAX_STEP<<4) < statusActive.step) {
      leds[a] = CHSV(statusActive.hue, statusActive.saturation, 0xFF);
    }
  }
}

void ICACHE_RAM_ATTR twinkleFade() {
  leds.fadeToBlackBy(statusActive.width);
}

// ----- Waves -----
void enablePrgWaves() {
  Serial.println("Enable Program Waves");
  waveTimer.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    waveDraw);
}

void disablePrgWaves() {
  Serial.println("Disable Program Waves");
  waveTimer.detach();
}

void ICACHE_RAM_ATTR waveDraw() {
  uint8_t numHumps = max((uint8_t)1,(uint8_t)statusActive.width);
  uint16_t numRows = WSLEDS / min((uint16_t)numHumps, (uint16_t)WSLEDS);
  uint8_t circleInterval = 0xFF / numHumps;
  uint8_t wavesFold = 1;
  // Serial.print("numHumps=");
  // Serial.println(numHumps);
  // Serial.print("numRows=");
  // Serial.println(numRows);

  static uint8_t pos; // Offset for start of sine wave

  leds.fill_solid(CRGB::Black);

  // j steps through each hump
  // numHumps = 1 ==> One cycle through this llop
  for (int j = 0; j < numHumps; j++) {
    // Serial.print("j=");
    // Serial.println(j);
    int fadePct = pos + (j*circleInterval);
    for (int f=0; f<wavesFold; f++) {
      fadePct = cubicwave8(fadePct);
    }
    // Serial.print("fadepct=");
    // Serial.println(fadePct);
    CRGB colColor = CHSV(statusActive.hue, statusActive.saturation, 0xFF);
    colColor.fadeToBlackBy(fadePct);
    for (int k = 0; k < (numRows+1); k++) {
      if (j+k*numHumps<WSLEDS) {
        leds[ j + k*numHumps ] = colColor;
      }
      // Serial.print("k=");
      // Serial.print(k);
      // Serial.print("; changing LED:");
      // Serial.print(j+k*numHumps);
      // Serial.print("; Color:");
      // Serial.println(colColor, HEX);
    }
  }
  pos += statusActive.step;
  // pos += 2;
}

// ----- Waves 2 -----
void enablePrgWaves2() {
  Serial.println("Enable Program Waves 2");
  wave2Timer.attach_ms(statusActive.refresh_period_ms, wave2Draw);
}

void disablePrgWaves2() {
  Serial.println("Disable Program Waves 2");
  wave2Timer.detach();
}

void ICACHE_RAM_ATTR wave2Draw() {
  // uint16_t step = statusActive.width;
  // static uint16_t pos;
  // uint16_t hueDelta = 0;

  // pos += statusActive.step;

  // leds.fill_solid(CRGB::Black);  // Clear strip

  // for (uint16_t j = 0; j < WSLEDS; j++) {
  //   if (j<WSLEDS) {
  //     leds[j] = CHSV(statusActive.hue+hueDelta+pos, statusActive.saturation, beatsin8(statusActive.speed, 0, 0xFF, pos));
  //   }
  //   hueDelta += statusActive.step;
  // }
  uint16_t step = statusActive.width;
  uint32_t pos = 0;
  static uint8_t hueDelta_start;
  uint8_t hueDelta = hueDelta_start;

  if (statusActive.step==0) {
    hueDelta_start = 0;
    hueDelta = 0;
  }

  leds.fill_solid(CRGB::Black);  // Clear strip

  for (uint16_t j = 0; j < WSLEDS; j++) {
    if (j<WSLEDS) {
      leds[j] = CHSV(statusActive.hue+hueDelta, statusActive.saturation, beatsin8(statusActive.speed, 0, 0xFF, pos));
    }
    pos += step;
    hueDelta += statusActive.step;
  }
  hueDelta_start += statusActive.step;

}

// ----- Dots -----
void enablePrgDots() {
  Serial.println("Enable Program Dots");
  dotTimer.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    dotDraw);
}

void disablePrgDots() {
  Serial.println("Disable Program Dots");
  dotTimer.detach();
}

void dotDraw() {

}

// ----- Twinkle Rainbow -----
void enablePrgTwinkleR() {
  Serial.println("Enable Program Twinkle Rainbow");
  random16_set_seed((uint16_t)ESP.getCycleCount());
  twinkleRTicker.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    makeTwinkleR);

  twinkleRFadeTicker.attach_ms(
    map(
      statusActive.speed,
      WLEDC_MIN_SPEED,
      WLEDC_MAX_SPEED,
      WLEDC_MAX_SPEED,
      max(MIN_SPEED_ALT, WLEDC_MIN_SPEED)
    ),
    twinkleRFade);
}

void disablePrgTwinkleR() {
  Serial.println("Disable Program Twinkle Rainbow");
  twinkleRTicker.detach();
  twinkleRFadeTicker.detach();
}

void ICACHE_RAM_ATTR makeTwinkleR() {
  // Iterate through all LEDS, choose random one to twinkle.
  // Hue set randomly
  // Saturation set between 0 and preference
  // Brightness set between 0 and preference
  for (uint16_t a=0; a<WSLEDS; a++) {
    if (random16(WLEDC_MAX_STEP<<4)<statusActive.step) {
      leds[a] = CHSV(random8(), random8(statusActive.saturation), random8(0xFF));
    }
  }
}

void ICACHE_RAM_ATTR twinkleRFade() {
  leds.fadeToBlackBy(statusActive.width);
}

// ----- Disable All -----
void ICACHE_RAM_ATTR disableAllPrg() {
  Serial.println("Disable Program All");

  disablePrgBlack();
  disablePrgWhite50();
  disablePrgRainbow();
  disablePrgTwinkle();
  disablePrgWaves();
  disablePrgDots();
  disablePrgWaves2();
  disablePrgTwinkleR();

  leds.fill_solid(CRGB::Black);
  FastLED.show();
}

void printState(status_t state) {
  Serial.print("P:");
  Serial.print(static_cast<uint8_t>(state.program));
  Serial.print(" A:");
  Serial.print(state.active);
  Serial.print(" W:");
  Serial.print(state.width);
  Serial.print(" H:");
  Serial.print(state.hue);
  Serial.print(" T:");
  Serial.print(state.saturation);
  Serial.print(" C:");
  Serial.print(state.step);
  Serial.print(" B:");
  Serial.print(state.maxbright);
  Serial.print(" S:");
  Serial.print(state.speed);
  Serial.println();
}

void printMem() {
  Serial.print("H ");
  Serial.println(ESP.getMaxFreeBlockSize());
}