enum command_type_t : uint8_t {
    WLEDC_CMD_NULL,
    WLEDC_CMD_OFF,
    WLEDC_CMD_GETSTATUS,
    WLEDC_CMD_SETSTATUS,
    WLEDC_CMD_STATUS,
    WLEDC_CMD_PING,
    WLEDC_CMD_PONG
};

enum program_t : uint8_t {
  WLEDC_PRG_BLACK,
  WLEDC_PRG_WHITE50,
  WLEDC_PRG_RAINBOW,
  WLEDC_PRG_TWINKLE,
  WLEDC_PRG_WAVES
};

struct __attribute__((packed)) status_t
{
  uint8_t active;
  uint8_t hue;
  uint8_t saturation;
  program_t program;
  uint16_t speed;
  uint16_t width;
  uint16_t refresh_period_ms;
  uint16_t maxbright;
  int16_t step;
};

struct __attribute__((packed)) command_t
{
  command_type_t cmd;
  // uint32_t timestamp;
  status_t stat;
};

#define WLEDC_MAX_SPEED  255
#define WLEDC_MIN_SPEED 0
#define WLEDC_MAX_WIDTH 255
#define WLEDC_MIN_WIDTH 0
#define WLEDC_MAX_HUE 255
#define WLEDC_MIN_HUE 0
#define WLEDC_MAX_SATURATION 255
#define WLEDC_MIN_SATURATION 0
#define WLEDC_MAX_REFRESH 2000
#define WLEDC_MIN_REFRESH 4
#define WLEDC_MAX_STEP 2000
#define WLEDC_MIN_STEP -2000
#define WLEDC_MAX_BRIGHT 255
#define WLEDC_MIN_BRIGHT 0

#define DEFAULT_SPEED 20
#define DEFAULT_WIDTH 1
#define DEFAULT_HUE 0
#define DEFAULT_SATURATION 255
#define DEFAULT_STEP 2
#define DEFAULT_BRIGHT 32
#define DEFAULT_PROGRAM WLEDC_PRG_BLACK
#define DEFAULT_ACTIVE 0
#define DEFAULT_REFRESH 17