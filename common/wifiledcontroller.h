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
  uint8_t maxbright;
  uint16_t count;
};

struct __attribute__((packed)) command_t
{
  command_type_t cmd;
  uint32_t timestamp;
  status_t stat;
};

