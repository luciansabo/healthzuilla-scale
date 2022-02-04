// comment/undef this to disable debugging
//#define DEBUG 1
#include "ssl/ssl-config.h";

// Pins
#define LCD_DC_PIN      D6 // scale DC pin
#define PW_SW_PIN       D0 // GPIO16 is controlling the power. LOW - power on, HIGH - power off
#define SCALE_DOUT_PIN  5  // scale DOUT on GPIO5
#define SCALE_CLK_PIN   4  // scale CLK on GPIO4
#define TARE_BTN_PIN    D8  // Tare btn pin
#define LOGO_LED_PIN    D3  // logo LED on GPIO0 (HIGH on boot)

#define DEFAULT_POWEROFF_SEC        300
#define DEFAULT_IDLE_POWEROFF_SEC   90
#define WIFI_CONNECT_TIMEOUT        (15 * 1000) // 15s
#define SHORT_PRESS_TIME            30 // ms

// ID of the settings block
#define CONFIG_VERSION "ls1"

// Tell it where to store your config data in EEPROM
#define CONFIG_START_ADDR 0

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 5

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

const char configPortalAP[] = "Healthzuilla-Scale";
#define SETUP_TIMEOUT             300 // Timeout for Config portal: 5 min
#define READ_INTERVAL             1000 // 1s scale reading interval
#define LOW_BATTERY_BLINK_DELAY   3000 // 4s delay between low battery blinks*/
