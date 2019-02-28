enum command_type_t : uint8_t {
    WLEDC_CMD_NULL,
    WLEDC_CMD_OFF,
    WLEDC_CMD_GETSTATUS,
    WLEDC_CMD_SETSTATUS,
    WLEDC_CMD_STATUS,
    WLEDC_CMD_PING,
    WLEDC_CMD_PONG
};

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

