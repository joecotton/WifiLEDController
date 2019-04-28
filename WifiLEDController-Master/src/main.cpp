#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <Button2.h>
#include <Encoder.h>
#include <U8g2lib.h>

extern "C"
{
#include <espnow.h>
#include <user_interface.h>
}

#include "../../common/wifiledcontroller.h"

char* menuTitles[] = {
  "Program",       // 0
  "Speed",         // 1
  "Width",         // 2
  "Step",          // 3
  "Hue",           // 4
  "Saturation",    // 5
  "Brightness",    // 6
  "Update Speed"   // 7
};
  // "Active"         // 8

char* programNames[] = {
  "Black",          // 0
  "White",          // 1
  "Rainbow",        // 2
  "Twinkle",        // 3
  "Waves",          // 4
  "Dots",           // 5
  "Waves 2",        // 6
  "Twinkle Rainbow" // 7
};

const uint8_t MENUCOUNT = 8;
uint8_t currentMenu = 0;
uint8_t menuDirty = 1;

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
#define STATUS_LED D3
#define ROTARY_A D5
#define ROTARY_B D6
#define ROTARY_BUTTON D7
#define ACTIVE_SWITCH D3

#define BAR_LEFT 0
#define BAR_RIGHT (127 - 7 - 1 - 7 - 1)
#define BAR_TOP 26
#define BAR_BOTTOM 31
#define BAR_MIDDLE (((BAR_TOP - BAR_BOTTOM) / 2) + BAR_BOTTOM-1)
#define BAR_WIDTH (BAR_RIGHT-BAR_LEFT+1)
#define BAR_HEIGHT  (BAR_BOTTOM-BAR_TOP+1)

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
void handlePendingSendStatus();
void handlePendingRecvStatus();

void handleMenu();
void printMenuState(uint8_t selected);
void printStatusValue(uint8_t selected);
void printState(status_t state);

void sendCommand(command_type_t command);
void sendPing();

void watchdogReset();
void watchdogExpire();

// void click(Button2& btn);
void activeSwitchHandle(Button2& btn);
void rotary_loop(int16_t);
void handleRotary();

void handleDisplay();
void displayDrawCurrentMenu();
void displayDrawCurrentValue();
void displayDrawSummary();
void drawMeter();

uint8_t volatile pendingStatusIn = 0;
uint8_t volatile pendingCommand = 0;
uint8_t volatile pendingCommandSending = 0;
uint8_t isConnected = 0;
uint8_t pendingPongIn = 0;
uint8_t volatile pendingStatusOut = 0;
uint8_t initialStatusGet = 3;

// uint32_t remoteTimestamp = 0L;

uint32_t position = 0;
uint8_t buttonState = 0;

Ticker reqStatusTicker;
Ticker ledTicker;
Ticker toggleActiveTicker;
Ticker watchdogTicker;
Ticker pingTicker;

Encoder rotary(ROTARY_A, ROTARY_B);
Button2 button = Button2(ROTARY_BUTTON, INPUT_PULLUP, 20U);
Button2 activeSwitch = Button2(ACTIVE_SWITCH, INPUT_PULLUP, 20U);

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA);   // pin remapping with ESP8266 HW I2C

uint8_t displayDirty = 1;

void setup()
{
  // pinMode(STATUS_LED, OUTPUT);
  // digitalWrite(STATUS_LED, LOW);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  u8g2.begin();

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

  activeSwitch.setChangedHandler(activeSwitchHandle);

  statusLocal.active             = DEFAULT_ACTIVE;
  statusLocal.program            = DEFAULT_PROGRAM;
  statusLocal.speed              = DEFAULT_SPEED;
  statusLocal.width              = DEFAULT_WIDTH;
  statusLocal.hue                = DEFAULT_HUE;
  statusLocal.saturation         = DEFAULT_SATURATION;
  statusLocal.refresh_period_ms  = DEFAULT_REFRESH;
  statusLocal.maxbright          = DEFAULT_BRIGHT;
  statusLocal.step               = DEFAULT_STEP;

  pingTicker.attach_ms(598, sendPing);

  requestStatus();
}

void loop()
{
  handleRotary();
  handlePendingRecvStatus();
  handlePendingSendStatus();
  handlePong();
  handleMenu();
  handleCommand();
  handleDisplay();
  
  button.loop();
  activeSwitch.loop();

  // if (isConnected) {
  //   digitalWrite(STATUS_LED, HIGH);
  // } else {
  //   digitalWrite(STATUS_LED, LOW);
  // }

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
  ledTicker.once_ms(80, flickLED);
  pendingCommandSending = 0;
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
  ledTicker.once_ms(8, flickLED);
  // watchdogReset();
}

void flickLED() {
  // ledTicker.detach();
  digitalWrite(LED_BUILTIN, HIGH);
}

void toggleActiveAndSend() {
  statusLocal.active = !(statusLocal.active);
  sendCommand(WLEDC_CMD_SETSTATUS);
}

void requestStatus() {
  sendCommand(WLEDC_CMD_GETSTATUS);
}

void sendCommand(command_type_t command) {
  // Serial.println("Sending requested Status");
  if (!pendingCommandSending) {
    commandBufferOut.cmd=command;
    // commandBufferOut.timestamp=millis();
    memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));

    if (command == WLEDC_CMD_GETSTATUS || command == WLEDC_CMD_PING) {
      pendingStatusIn = 1;
    }

    esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
  }
}

void handlePendingSendStatus() {
  if (pendingStatusOut) {
    sendCommand(WLEDC_CMD_SETSTATUS);
    reqStatusTicker.once_ms(100, requestStatus);

    sendCommand(WLEDC_CMD_GETSTATUS);
    pendingStatusOut = 0;
  }
}

void handlePendingRecvStatus() {
  // Copy the remote status to our local copy
  if (pendingStatusIn) {
    if (initialStatusGet) {
      memcpy(&statusLocal, &statusRemote, sizeof(statusLocal));
      initialStatusGet--;
    }
    pendingStatusIn = 0;
  }
}

void sendPing() {
  sendCommand(WLEDC_CMD_PING);
}

void handlePong() {
  if (pendingPongIn) {
    pendingPongIn = 0;
    // Reset watchdog timer
    watchdogReset();
  }
}

void watchdogExpire() {
  // Serial.println("Watchdog Expired...");
  isConnected = 0;
  displayDirty = 1;
}

void watchdogReset() {
  isConnected = 1;
  displayDirty = 1;
  watchdogTicker.detach();
  watchdogTicker.attach_ms(1200, watchdogExpire);
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
      // Contains remote status
      break;
    case WLEDC_CMD_PING:
      // Server doesn't get PINGed
      break;
    case WLEDC_CMD_PONG:
      // Serial.println("PONG!");
      pendingPongIn = 1;
      break;
    };

    // Copy status that was returned from the client
    memcpy(&statusRemote, &(commandBufferIn.stat), sizeof(commandBufferIn.stat));

    // remoteTimestamp = commandBufferIn.timestamp;

    pendingCommand = 0;
    // watchdogReset();

    displayDirty = 1;
  }
}

// void click(Button2& btn) {
//   // Serial.println("click\n");
// }

void activeSwitchHandle(Button2& btn) {
  if (btn.isPressed()) {
    statusLocal.active = 1;
  } else {
    statusLocal.active = 0;
  }
  // statusLocal.active = !(statusLocal.active);
  pendingStatusOut = 1;
  menuDirty = 1;
  displayDirty = 1;
}

void handleRotary() {
  static int16_t lastPos;
  static int16_t d;
  int16_t newPos = rotary.read() >> 2;
  d = (newPos - lastPos);
  lastPos = newPos;
  rotary_loop(d);
}

void rotary_loop(int16_t delta) {
	//lets see if anything changed
  int8_t dir = (delta>0)? 1 : -1;
	
	//optionally we can ignore whenever there is no change
	if (delta == 0) return;
	

	//first lets handle rotary encoder button click
	if (button.isPressed()) {
    displayDirty = 1;
    // Button is pressed
    // Change the current menuy
    if (dir>0) {

      // Clockwise
      currentMenu++;
      if (currentMenu >= MENUCOUNT) {
        // At top
        currentMenu = 0;
      }
    } else {
      // Counter-clockwise
      if (currentMenu==0) {
        // At bottom, resstart to top
        currentMenu = MENUCOUNT;
      }
      currentMenu--;
    }
	} else {
    // Change current value
    // Button is not pressed
    // Change the value of current menu item
    switch (currentMenu) {
      case 0: // Program
        if (dir<0) {
          --statusLocal.program;
        } else {
          ++statusLocal.program;
        }
        break;
      case 1: // Speed
        if (dir<0)
          statusLocal.speed = max(statusLocal.speed + delta, WLEDC_MIN_SPEED);
        else
          statusLocal.speed = min(statusLocal.speed + delta, WLEDC_MAX_SPEED);
        break;
      case 2: // Width
        if (dir<0)
          statusLocal.width = max(statusLocal.width + delta, WLEDC_MIN_WIDTH);
        else
          statusLocal.width = min(statusLocal.width + delta , WLEDC_MAX_WIDTH);
        break;
      case 3:  // Step
        if (dir < 0)
          statusLocal.step = max(statusLocal.step + delta, WLEDC_MIN_STEP);
        else
          statusLocal.step = min(statusLocal.step + delta, WLEDC_MAX_STEP);
        break;
      case 4:  // Hue
        if (dir<0)
          statusLocal.hue = max(statusLocal.hue + delta, WLEDC_MIN_HUE);
        else
          statusLocal.hue = min(statusLocal.hue + delta, WLEDC_MAX_HUE);
        break;
      case 5: // Saturation
        if (dir<0)
          statusLocal.saturation = max(statusLocal.saturation + delta, WLEDC_MIN_SATURATION);
        else
          statusLocal.saturation = min(statusLocal.saturation + delta, WLEDC_MAX_SATURATION);
        break;
      case 6: // Brightness
        if (dir<0)
          statusLocal.maxbright = max(statusLocal.maxbright + delta, WLEDC_MIN_BRIGHT);
        else
          statusLocal.maxbright = min(statusLocal.maxbright + delta, WLEDC_MAX_BRIGHT);
        break;
      case 7: // Refresh Period
        if (dir<0)
          statusLocal.refresh_period_ms = max(statusLocal.refresh_period_ms + delta, WLEDC_MIN_REFRESH);
        else
          statusLocal.refresh_period_ms = min(statusLocal.refresh_period_ms + delta, WLEDC_MAX_REFRESH);
        break;
      case 8: // Active
        if (dir<0)
          statusLocal.active = 0;
        else
          statusLocal.active = 1;
        break;
    }

    pendingStatusOut = 1;
  }
  menuDirty = 1;
  displayDirty = 1;
}

void handleMenu() {
  if (menuDirty) {
    printMenuState(currentMenu);
    menuDirty = 0;
  }
}

void printMenuState(uint8_t selected) {
    Serial.print(menuTitles[selected]);
    Serial.print(": ");
    printStatusValue(selected);
    Serial.println();
}

void printStatusValue(uint8_t selected) {
  switch (selected) {
    case 0: // Program
      Serial.print(static_cast<uint8_t>(statusLocal.program));
      Serial.print("/");
      Serial.print(static_cast<uint8_t>(statusRemote.program));
      break;
    case 1: // Speed
      Serial.print(statusLocal.speed);
      Serial.print("/");
      Serial.print(statusRemote.speed);
      break;
    case 2: // Width
      Serial.print(statusLocal.width);
      Serial.print("/");
      Serial.print(statusRemote.width);
      break;
    case 3:  // Step
      Serial.print(statusLocal.step);
      Serial.print("/");
      Serial.print(statusRemote.step);
      break;
    case 4:  // Hue
      Serial.print(statusLocal.hue);
      Serial.print("/");
      Serial.print(statusRemote.hue);
      break;
    case 5: // Saturation
      Serial.print(statusLocal.saturation);
      Serial.print("/");
      Serial.print(statusRemote.saturation);
      break;
    case 6: // Brightness
      Serial.print(statusLocal.maxbright);
      Serial.print("/");
      Serial.print(statusRemote.maxbright);
      break;
    case 7: // Refresh Period
      Serial.print(statusLocal.refresh_period_ms);
      Serial.print("/");
      Serial.print(statusRemote.refresh_period_ms);
      break;
    case 8: // Active
      Serial.print(statusLocal.active);
      Serial.print("/");
      Serial.print(statusRemote.active);
      break;
  }
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

void handleDisplay() {
  if (displayDirty) {
    u8g2.clearBuffer();

    // u8g2.drawStr(0, 32, statusLocal.refresh_period_ms);
    displayDrawCurrentMenu();
    displayDrawSummary();
    displayDrawCurrentValue();

    u8g2.sendBuffer();

    displayDirty = 0;
  }
}

void displayDrawCurrentMenu() {
  u8g2.setFont(u8g2_font_7x13_t_symbols);
  u8g2.setDrawColor(1);
  u8g2.drawStr(0, 13, menuTitles[currentMenu]);
}

void displayDrawCurrentValue() {
  drawMeter();
}

void drawMeter() {
  uint8_t barLength;
  char value[6];
  switch (currentMenu) {
    case 0:  // Program
      // Do not draw bar, draw name of current program
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      u8g2.setCursor(BAR_LEFT, BAR_BOTTOM);
      u8g2.print(programNames[static_cast<uint8_t>(statusLocal.program)]);
      break;
    case 1:  // Speed
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.speed, WLEDC_MIN_SPEED, WLEDC_MAX_SPEED, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.speed, WLEDC_MIN_SPEED, WLEDC_MAX_SPEED, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      // Values; Local/Remote
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.speed, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.speed, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 2:  // Width
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.width, WLEDC_MIN_WIDTH, WLEDC_MAX_WIDTH, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.width, WLEDC_MIN_WIDTH, WLEDC_MAX_WIDTH, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.width, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.width, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 3:  // Step
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.step, WLEDC_MIN_STEP, WLEDC_MAX_STEP, 0,
                      BAR_WIDTH - 2);
      u8g2.drawBox(BAR_LEFT + 1, BAR_TOP + 1, barLength, BAR_MIDDLE - BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.step, WLEDC_MIN_STEP, WLEDC_MAX_STEP, 0,
                      BAR_WIDTH - 2);
      u8g2.drawBox(BAR_LEFT + 1, BAR_MIDDLE + 1, barLength,
                   BAR_BOTTOM - BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.step, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.step, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 4:  // Hue
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.hue, WLEDC_MIN_HUE, WLEDC_MAX_HUE, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.hue, WLEDC_MIN_HUE, WLEDC_MAX_HUE, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.hue, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.hue, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 5:  // Saturation
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.saturation, WLEDC_MIN_SATURATION, WLEDC_MAX_SATURATION, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.saturation, WLEDC_MIN_SATURATION, WLEDC_MAX_SATURATION, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.saturation, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.saturation, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 6:  // Brightness
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.maxbright, WLEDC_MIN_BRIGHT, WLEDC_MAX_BRIGHT, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.maxbright, WLEDC_MIN_BRIGHT, WLEDC_MAX_BRIGHT, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.maxbright, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.maxbright, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 7:  // Refresh Period
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.refresh_period_ms, WLEDC_MIN_REFRESH, WLEDC_MAX_REFRESH, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.refresh_period_ms, WLEDC_MIN_REFRESH, WLEDC_MAX_REFRESH, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.refresh_period_ms, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 9);
      u8g2.print(value);
      u8g2.print(F("L"));
      itoa(statusRemote.refresh_period_ms, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.setCursor((120 - barLength), 20);
      u8g2.print(value);
      u8g2.print(F("R"));
      break;
    case 8:  // Active
      // Do not draw bar, draw ACTIVE/INACTIVE
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      u8g2.setCursor(0, 31);
      if (statusLocal.active) {
        u8g2.print(F("ON"));
      } else {
        u8g2.print(F("OFF"));
      }
      u8g2.setCursor(50, 31);
      if (statusRemote.active) {
        u8g2.print(F("/ ON"));
      } else {
        u8g2.print(F("/ OFF"));
      }
      break;
  }
}

void displayDrawSummary() {
  // //Other marks
  u8g2.setFont(u8g2_font_7x13_t_symbols);
  u8g2.setDrawColor(1);
  if (isConnected) {
    u8g2.drawGlyph((127-6), (33), 0x25cf);
  } else {
    u8g2.drawGlyph((127-6), (33), 0x25cb);
  }
  if (statusLocal.active) {
    u8g2.drawGlyph((127-6-6-1), (33), 0x2600);
  }
}
