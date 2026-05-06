#pragma once

// LilyGo T-Deck Plus pin assignments.
// Display+SD+LoRa share SPI bus (MOSI=41, MISO=38, SCK=40).
// Keyboard+Touch share I2C bus Wire1 (SDA=18, SCL=8).

// Shared SPI bus (Display + SD + LoRa)
#define TDECK_SPI_MOSI   41
#define TDECK_SPI_MISO   38
#define TDECK_SPI_SCK    40

// ST7789 display (320x240)
#define TDECK_TFT_CS     12
#define TDECK_TFT_DC     11
#define TDECK_TFT_BL     42
#define TDECK_TFT_RST    -1

// SD card
#define TDECK_SD_CS      39

// SX1262 LoRa radio
#define TDECK_LORA_CS     9
#define TDECK_LORA_RST   17
#define TDECK_LORA_DIO1  45
#define TDECK_LORA_BUSY  13

// Keyboard (I2C, ESP32-C3 co-processor at 0x55)
#define TDECK_KB_I2C_ADDR 0x55
#define TDECK_KB_SDA      18
#define TDECK_KB_SCL       8

// Trackball (5 GPIO: 4 directional ISRs + click)
#define TDECK_TRACKBALL_UP     3
#define TDECK_TRACKBALL_DOWN  15
#define TDECK_TRACKBALL_LEFT   1
#define TDECK_TRACKBALL_RIGHT  2
#define TDECK_TRACKBALL_CLICK  0

// Battery (direct ADC via 2:1 divider)
#define TDECK_BAT_ADC     4

// u-blox GPS (38400 baud)
#define TDECK_GPS_RX     44
#define TDECK_GPS_TX     43
#define TDECK_GPS_BAUD   38400

// Board power enable
#define TDECK_POWER_EN   10

// I2S speaker (MAX98357A)
#define TDECK_I2S_WS      5
#define TDECK_I2S_BCK     7
#define TDECK_I2S_DOUT    6

// GT911 capacitive touch (shares I2C bus with keyboard)
#define TDECK_TOUCH_INT  16
