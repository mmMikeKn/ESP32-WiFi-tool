#ifndef _TFT_ST37735_H_
#define _TFT_ST37735_H_

#define ST7735_RESET_GPIO 16
#define ST7735_AO_GPIO 17

#define ST7735_DMA_CHANNEL 1  // ither channel 1 or 2, or 0 in the case when no DMA is required.
#define ST7735_SPI_HOST HSPI_HOST
#define ST7735_MOSI_GPIO 13
#define ST7735_CLK_GPIO 14
#define ST7735_CS_GPIO 15

#define CONFIG_TFT_WIDTH 128
#define CONFIG_TFT_HEIGHT 128

#define ST7735_Black 0x0000     /*   0,   0,   0 */
#define ST7735_Navy 0x000F      /*   0,   0, 128 */
#define ST7735_DarkGreen 0x03E0 /*   0, 128,   0 */
#define ST7735_DarkCyan 0x03EF  /*   0, 128, 128 */
#define ST7735_Maroon 0x7800    /* 128,   0,   0 */
#define ST7735_Purple 0x780F    /* 128,   0, 128 */
#define ST7735_Olive 0x7BE0     /* 128, 128,   0 */
#define ST7735_LightGrey 0xC618 /* 192, 192, 192 */
#define ST7735_DarkGrey 0x7BEF  /* 128, 128, 128 */
#define ST7735_Blue 0x001F      /*   0,   0, 255 */
#define ST7735_Blue2 0x051F
#define ST7735_Green 0x07E0       /*   0, 255,   0 */
#define ST7735_Cyan 0x07FF        /*   0, 255, 255 */
#define ST7735_Red 0xF800         /* 255,   0,   0 */
#define ST7735_Magenta 0xF81F     /* 255,   0, 255 */
#define ST7735_Yellow 0xFFE0      /* 255, 255,   0 */
#define ST7735_White 0xFFFF       /* 255, 255, 255 */
#define ST7735_Orange 0xFD20      /* 255, 165,   0 */
#define ST7735_GreenYellow 0xAFE5 /* 173, 255,  47 */
#define ST7735_Pink 0xF81F

#define ST7735_CASET 0x2A
#define ST7735_RASET 0x2B
#define ST7735_RAMWR 0x2C

// #define CONFIG_TFT_BACKLIGHT_GPIO

#ifdef __cplusplus
extern "C" {
#endif

void st7735_Init();
void st7735_Clear(unsigned short color);
void st7735_BackLight(unsigned char perscentPWD);
uint8_t st7735_Text(uint8_t x, uint8_t y, const char *str, unsigned short fColor, unsigned short bkColor);
void st7735_RectangleFill(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, unsigned short color);
void st7735_Rectangle(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, unsigned short color);

void st7735_Img(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, const unsigned char *map);

#ifdef __cplusplus
}
#endif

#endif
