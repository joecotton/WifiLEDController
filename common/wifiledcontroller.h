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
  WLEDC_PRG_TWINKLE
};

struct __attribute__((packed)) status_t
{
  uint8_t active;
  program_t program;
  uint16_t speed;
  uint16_t width;
  uint16_t refresh_period_ms;
  uint16_t maxbright;
  uint16_t step;
};

struct __attribute__((packed)) command_t
{
  command_type_t cmd;
  uint32_t timestamp;
  status_t stat;
};

#define WLEDC_MAX_SPEED 255
#define WLEDC_MIN_SPEED 0
#define WLEDC_MAX_WIDTH 255
#define WLEDC_MIN_WIDTH 0
#define WLEDC_MAX_REFRESH 65535
#define WLEDC_MIN_REFRESH 4
#define WLEDC_MAX_STEP 255
#define WLEDC_MIN_STEP 0
#define WLEDC_MAX_BRIGHT 255
#define WLEDC_MIN_BRIGHT 0

#define DEFAULT_SPEED 20
#define DEFAULT_WIDTH 1
#define DEFAULT_STEP 2
#define DEFAULT_BRIGHT 32
#define DEFAULT_PROGRAM WLEDC_PRG_BLACK
#define DEFAULT_ACTIVE 0
#define DEFAULT_REFRESH 17