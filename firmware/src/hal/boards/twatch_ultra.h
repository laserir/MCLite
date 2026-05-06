#pragma once

// LilyGo T-Watch Ultra pin assignments.
// Display is on dedicated QSPI bus.
// LoRa + SD share regular SPI bus (MOSI=34, MISO=33, SCK=35).
// All sensors/PMU/touch share I2C bus (SDA=3, SCL=2).

// Shared SPI bus (LoRa + SD)
#define TWATCH_SPI_MOSI  34
#define TWATCH_SPI_MISO  33
#define TWATCH_SPI_SCK   35

// CO5300 AMOLED display (502x410), QSPI dedicated bus
#define TWATCH_DISP_CS   41
#define TWATCH_DISP_SCK  40
#define TWATCH_DISP_D0   38
#define TWATCH_DISP_D1   39
#define TWATCH_DISP_D2   42
#define TWATCH_DISP_D3   45
#define TWATCH_DISP_RST  37
#define TWATCH_DISP_TE    6

// SD card (shares SPI bus with LoRa)
#define TWATCH_SD_CS     21

// SX1262 LoRa radio (shares SPI bus with SD)
#define TWATCH_LORA_CS   36
#define TWATCH_LORA_RST  47
#define TWATCH_LORA_BUSY 48
#define TWATCH_LORA_IRQ  14

// CST9217 capacitive touch (I2C addr 0x1A on shared bus)
#define TWATCH_TOUCH_INT 12
#define TWATCH_TOUCH_I2C_ADDR 0x1A

// u-blox MIA-M10Q GPS (default 38400 baud, PPS available but unused)
#define TWATCH_GPS_TX    43
#define TWATCH_GPS_RX    44
#define TWATCH_GPS_PPS   13
#define TWATCH_GPS_BAUD  38400

// I2S speaker (MAX98357A)
#define TWATCH_I2S_BCK    9
#define TWATCH_I2S_WS    10
#define TWATCH_I2S_DOUT  11

// Shared I2C bus (PMU + RTC + IMU + haptic + expander + touch)
#define TWATCH_I2C_SDA    3
#define TWATCH_I2C_SCL    2

// Interrupts
#define TWATCH_PMU_INT    7    // AXP2101
#define TWATCH_RTC_INT    1    // PCF85063A
#define TWATCH_SENSOR_INT 8    // BHI260AP IMU
#define TWATCH_NFC_INT    5    // ST25R3916
#define TWATCH_NFC_CS     4

// I2C device addresses
#define TWATCH_AXP2101_ADDR  0x34
#define TWATCH_PCF85063_ADDR 0x51
#define TWATCH_BHI260_ADDR   0x28
#define TWATCH_DRV2605_ADDR  0x5A
#define TWATCH_XL9555_ADDR   0x20

// XL9555 I/O expander virtual GPIOs (controls power-on sequencing)
#define TWATCH_EXP_DRV_EN     0    // haptic enable
#define TWATCH_EXP_DISP_EN    7    // display power
#define TWATCH_EXP_TOUCH_RST  8    // touch controller reset
#define TWATCH_EXP_SD_DET    10    // SD card detect
