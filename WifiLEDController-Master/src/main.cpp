#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
// #include <ESPRotary.h>
// #include "AiEsp32RotaryEncoder.h"
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
  "Program",        // 0
  "Speed",          // 1
  "Width",          // 2
  "Step",           // 3
  "Brightness",     // 4
  "Update Speed",   // 5
  "Active"          // 6
};

char* programNames[] = {
  "Black",          // 0
  "White 50%",      // 1
  "Rainbow",        // 2
  "Twinkle"         // 3
};

const uint8_t MENUCOUNT = 7;
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

// #define ROTARY_ENCODER_A_PIN D3
// #define ROTARY_ENCODER_B_PIN D4
// #define ROTARY_ENCODER_BUTTON_PIN D7
// #define ROTARY_ENCODER_VCC_PIN -1 /*put -1 of Rotary encoder Vcc is connected directly to 3,3V; else you can use declared output pin for powering rotary encoder */

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

// void rotate(ESPRotary& r);
// void showDirection(ESPRotary& r);
// void handleRotate(ESPRotary& r);
// void showPosition(Button2& btn);
// void click(Button2& btn);
void rotary_loop(int16_t);
void handleRotary();

void handleDisplay();
void displayDrawCurrentMenu();
void displayDrawCurrentValue();
void displayDrawSummary();
void drawMeter();

uint8_t pendingStatusIn = 0;
uint8_t pendingCommand = 0;
uint8_t isConnected = 0;
uint8_t pendingPongIn = 0;
uint8_t pendingStatusOut = 0;
uint8_t initialStatusGet = 3;

uint32_t remoteTimestamp = 0L;

uint32_t position = 0;
uint8_t buttonState = 0;

Ticker reqStatusTicker;
Ticker ledTicker;
Ticker toggleActiveTicker;
Ticker watchdogTicker;
Ticker pingTicker;

// ESPRotary rotary = ESPRotary(ROTARY_A, ROTARY_B, 4);
Encoder rotary(ROTARY_A, ROTARY_B);
Button2 button = Button2(ROTARY_BUTTON, INPUT_PULLUP, 20U);
// AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN, ROTARY_ENCODER_VCC_PIN);

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ SCL, /* data=*/ SDA);   // pin remapping with ESP8266 HW I2C

uint8_t displayDirty = 1;

void setup()
{
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
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

  // rotary.setChangedHandler(handleRotate);

  // button.setReleasedHandler(click);

  //we must initialize rorary encoder 
	// rotaryEncoder.begin();
	// rotaryEncoder.setup([]{rotaryEncoder.readEncoder_ISR();});

  statusLocal.active             = DEFAULT_ACTIVE;
  statusLocal.program            = DEFAULT_PROGRAM;
  statusLocal.speed              = DEFAULT_SPEED;
  statusLocal.width              = DEFAULT_WIDTH;
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
  
  // rotary.loop();
  button.loop();
	// rotary_loop();

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
  ledTicker.once_ms(80, flickLED);
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
  commandBufferOut.cmd=command;
  commandBufferOut.timestamp=millis();
  memcpy(&(commandBufferOut.stat), &statusLocal, sizeof(statusLocal));

  if (command == WLEDC_CMD_GETSTATUS || command == WLEDC_CMD_PING) {
    pendingStatusIn = 1;
  }

  esp_now_send(remoteMac, (uint8_t *)&commandBufferOut, sizeof(commandBufferOut));
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

    remoteTimestamp = commandBufferIn.timestamp;

    pendingCommand = 0;
    // watchdogReset();

    displayDirty = 1;
  }
}

// Encoder/Button
// void rotate(ESPRotary& r) {
//    Serial.println(r.getPosition());
// }

// void showPosition(Button2& btn) {
//   Serial.println(rotary.getPosition());
// }

// void click(Button2& btn) {
//   // Serial.println("click\n");
// }

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
        switch (statusLocal.program) {
          case WLEDC_PRG_BLACK:
          if (dir<0)
            statusLocal.program = WLEDC_PRG_TWINKLE;
          else
            statusLocal.program = WLEDC_PRG_WHITE50;
          break;
          case WLEDC_PRG_WHITE50:
          if (dir<0)
            statusLocal.program = WLEDC_PRG_BLACK;
          else
            statusLocal.program = WLEDC_PRG_RAINBOW;
          break;
          case WLEDC_PRG_RAINBOW:
          if (dir<0)
            statusLocal.program = WLEDC_PRG_WHITE50;
          else
            statusLocal.program = WLEDC_PRG_TWINKLE;
          break;
          case WLEDC_PRG_TWINKLE:
          if (dir<0)
            statusLocal.program = WLEDC_PRG_RAINBOW;
          else
            statusLocal.program = WLEDC_PRG_BLACK;
          break;
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
      case 3: // Step
        if (dir<0)
          statusLocal.step = max(statusLocal.step + delta, WLEDC_MIN_STEP);
        else
          statusLocal.step = min(statusLocal.step + delta, WLEDC_MAX_STEP);
        break;
      case 4: // Brightness
        if (dir<0)
          statusLocal.maxbright = max(statusLocal.maxbright + delta, WLEDC_MIN_BRIGHT);
        else
          statusLocal.maxbright = min(statusLocal.maxbright + delta, WLEDC_MAX_BRIGHT);
        break;
      case 5: // Refresh Period
        if (dir<0)
          statusLocal.refresh_period_ms = max(statusLocal.refresh_period_ms + delta, WLEDC_MIN_REFRESH);
        else
          statusLocal.refresh_period_ms = min(statusLocal.refresh_period_ms + delta, WLEDC_MAX_REFRESH);
        break;
      case 6: // Active
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

// void handleRotate(ESPRotary& r) {
//   if (button.isPressed()) {
//     displayDirty = 1;
//     // Button is pressed
//     // Change the current menuy
//     if (r.getDirection() == RE_LEFT) {
//       // Counter-clockwise
//       if (currentMenu==0) {
//         // At bottom, resstart to top
//         currentMenu = MENUCOUNT;
//       }
//       currentMenu--;
//     } else {
//       // Clockwise
//       currentMenu++;
//       if (currentMenu >= MENUCOUNT) {
//         // At top
//         currentMenu = 0;
//       }
//     }
//   } else {
//     // Button is not pressed
//     // Change the value of current menu item
//     switch (currentMenu) {
//       case 0: // Program
//         switch (statusLocal.program) {
//           case WLEDC_PRG_BLACK:
//           if (r.getDirection() == RE_LEFT)
//             statusLocal.program = WLEDC_PRG_TWINKLE;
//           else
//             statusLocal.program = WLEDC_PRG_WHITE50;
//           break;
//           case WLEDC_PRG_WHITE50:
//           if (r.getDirection() == RE_LEFT)
//             statusLocal.program = WLEDC_PRG_BLACK;
//           else
//             statusLocal.program = WLEDC_PRG_RAINBOW;
//           break;
//           case WLEDC_PRG_RAINBOW:
//           if (r.getDirection() == RE_LEFT)
//             statusLocal.program = WLEDC_PRG_WHITE50;
//           else
//             statusLocal.program = WLEDC_PRG_TWINKLE;
//           break;
//           case WLEDC_PRG_TWINKLE:
//           if (r.getDirection() == RE_LEFT)
//             statusLocal.program = WLEDC_PRG_RAINBOW;
//           else
//             statusLocal.program = WLEDC_PRG_BLACK;
//           break;
//         }
//         break;
//       case 1: // Speed
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.speed = max(statusLocal.speed - 1, WLEDC_MIN_SPEED);
//         else
//           statusLocal.speed = min(statusLocal.speed + 1, WLEDC_MAX_SPEED);
//         break;
//       case 2: // Width
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.width = max(statusLocal.width - 1, WLEDC_MIN_WIDTH);
//         else
//           statusLocal.width = min(statusLocal.width + 1 , WLEDC_MAX_WIDTH);
//         break;
//       case 3: // Step
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.step = max(statusLocal.step - 1, WLEDC_MIN_STEP);
//         else
//           statusLocal.step = min(statusLocal.step + 1, WLEDC_MAX_STEP);
//         break;
//       case 4: // Brightness
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.maxbright = max(statusLocal.maxbright - 1, WLEDC_MIN_BRIGHT);
//         else
//           statusLocal.maxbright = min(statusLocal.maxbright + 1, WLEDC_MAX_BRIGHT);
//         break;
//       case 5: // Refresh Period
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.refresh_period_ms = max(statusLocal.refresh_period_ms - 1, WLEDC_MIN_REFRESH);
//         else
//           statusLocal.refresh_period_ms = min(statusLocal.refresh_period_ms + 1, WLEDC_MAX_REFRESH);
//         break;
//       case 6: // Active
//         if (r.getDirection() == RE_LEFT)
//           statusLocal.active = 0;
//         else
//           statusLocal.active = 1;
//         break;
//     }

//     pendingStatusOut = 1;
//   }
//   menuDirty = 1;
//   displayDirty = 1;
// }

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
      Serial.print(statusLocal.program);
      Serial.print("/");
      Serial.print(statusRemote.program);
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
    case 3: // Count
      Serial.print(statusLocal.step);
      Serial.print("/");
      Serial.print(statusRemote.step);
      break;
    case 4: // Brightness
      Serial.print(statusLocal.maxbright);
      Serial.print("/");
      Serial.print(statusRemote.maxbright);
      break;
    case 5: // Refresh Period
      Serial.print(statusLocal.refresh_period_ms);
      Serial.print("/");
      Serial.print(statusRemote.refresh_period_ms);
      break;
    case 6: // Active
      Serial.print(statusLocal.active);
      Serial.print("/");
      Serial.print(statusRemote.active);
      break;
  }
}

void printState(status_t state) {
  Serial.print("P:");
  Serial.print(state.program);
  Serial.print(" A:");
  Serial.print(state.active);
  Serial.print(" W:");
  Serial.print(state.width);
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
      u8g2.drawStr(BAR_LEFT, BAR_BOTTOM, programNames[statusLocal.program]);
      break;
    case 1:  // Speed
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.speed, WLEDC_MIN_SPEED, WLEDC_MAX_SPEED, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.speed, WLEDC_MIN_SPEED, WLEDC_MAX_SPEED, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.speed, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.drawStr((127-barLength), 9, value);
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
      u8g2.drawStr((127-barLength), 9, value);
      break;
    case 3:  // Step
      u8g2.drawFrame(BAR_LEFT, BAR_TOP, BAR_WIDTH, BAR_HEIGHT);
      // Top bar, local status
      barLength = map(statusLocal.step, WLEDC_MIN_STEP, WLEDC_MAX_STEP, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_TOP+1, barLength, BAR_MIDDLE-BAR_TOP);
      // Bottom Bar, remote status
      barLength = map(statusRemote.step, WLEDC_MIN_STEP, WLEDC_MAX_STEP, 0, BAR_WIDTH-2);
      u8g2.drawBox(BAR_LEFT+1, BAR_MIDDLE+1, barLength, BAR_BOTTOM-BAR_MIDDLE);
      u8g2.setFont(u8g2_font_7x13_t_symbols);
      u8g2.setDrawColor(1);
      itoa(statusLocal.step, value, 10);
      barLength = u8g2.getStrWidth(value);
      u8g2.drawStr((127-barLength), 9, value);
      break;
    case 4:  // Brightness
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
      u8g2.drawStr((127-barLength), 9, value);
      break;
    case 5:  // Refresh Period
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
      u8g2.drawStr((127-barLength), 9, value);
      break;
    case 6:  // Active
      // Do not draw bar, draw ACTIVE/INACTIVE
      break;
  }
}

void displayDrawSummary() {
  // //Other marks
  u8g2.setFont(u8g2_font_7x13_t_symbols);
  u8g2.setDrawColor(1);
  if (isConnected) {
    // u8g2.drawStr(113, 14, (char *)0x71);
    u8g2.drawGlyph((127-6), (33), 0x25cf);
  } else {
    // u8g2.drawStr(113, 14, (char *)0x70);
    u8g2.drawGlyph((127-6), (33), 0x25cb);
  }
  if (statusLocal.active) {
    u8g2.drawGlyph((127-6-6-1), (33), 0x2600);
  }
}
