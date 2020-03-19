/* SPI Master example
 based on SDK examples/peripherals/spi_master/main/spi_master_example_main.c
*/

#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"

#include "font_8_16.h"
#include "tft_st7735.h"

DRAM_ATTR static uint8_t dmaBuffer[16 * CONFIG_TFT_HEIGHT * 2];
static spi_device_handle_t spi;

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static void lcd_cmd(const uint8_t cmd) {    
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));  //Zero out the transaction
    t.length = 8;              //Command is 8 bits
    dmaBuffer[0] = cmd;
    t.tx_buffer = dmaBuffer;                     //The data is the cmd itself
    t.user = (void *)0;                          //D/C needs to be set to 0
    while(spi_device_polling_transmit(spi, &t) == ESP_ERR_INVALID_STATE) {};
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
static void lcd_data4dmaBuf(int len) {
    spi_transaction_t t;
    if (len == 0) return;                        //no need to send anything
    memset(&t, 0, sizeof(t));                    //Zero out the transaction
    t.length = len * 8;                          //Len is in bytes, transaction length is in bits.
    t.tx_buffer = dmaBuffer;                     //Data
    t.user = (void *)1;                          //D/C needs to be set to 1
    while(spi_device_polling_transmit(spi, &t) == ESP_ERR_INVALID_STATE) {};
}

static void lcd_data(const uint8_t *data, int len) {
    assert(len < sizeof(dmaBuffer));
    if (dmaBuffer != data) memcpy(dmaBuffer, data, len);
    lcd_data4dmaBuf(len);
}

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes;  //No of data in data; bit 7..4 = delay after set; 0xFF = end of cmds.
} lcd_init_cmd_t;

typedef enum {
    LCD_TYPE_ILI = 1,
    LCD_TYPE_ST,
    LCD_TYPE_MAX,
} type_lcd_t;

static const lcd_init_cmd_t lcd_init_cmds[] = {
    {0x01, {0}, 0 | 0x20},  // Software reset + delay 100ms
    {0x11, {0}, 0 | 0xA0},  // Out of sleep mode + delay 500
    {0x29, {0}, 0 | 0x20},  // Main screen turn on. 100 ms delay
    //{0x36, {0x40|0x80|0x08}, 1}, // Memory access control (directions row addr/col addr, bottom to top refresh): MADCTL_MX | MADCTL_MY | MADCTL_BGR
    {0x36, {0x00 | 0x00 | 0x08}, 1},  // Memory access control (directions row addr/col addr, bottom to top refresh): MADCTL_MX | MADCTL_MY | MADCTL_BGR
    {0x3A, {5}, 1 | 0x10},            // Set color mode: 16-bit color + delay 50ms

    {0x13, {0}, 0},  // Normal Display ON
    ///*
    {0xB1, {0x01, 0x2C, 0x2D}, 3},                    //  Frame rate ctrl - normal mode: Rate = fosc/(1x2+40) * (LINE+2C+2D)
    {0xB2, {0x01, 0x2C, 0x2D}, 3},                    //  Frame rate control - idle mode: Rate = fosc/(1x2+40) * (LINE+2C+2D)
    {0xB3, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6},  // Frame rate ctrl - partial mode: Dot inversion mode+Line inversion mode
    {0xB4, {0x07}, 1},                                //  Display inversion ctrl:   No inversion
    {0xC0, {0xA2, 0x02, 0x84}, 3},                    // Power control: -4.6V, AUTO mode
    {0xC1, {0xC5}, 1},                                // Power control: VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
    {0xC2, {0x0A, 0x00}, 2},                          //  Power control: Opamp current small Boost frequency
    {0xC3, {0x8A, 0x2A}, 2},                          // Power control: BCLK/2, Opamp current small & Medium low
    {0xC4, {0x8A, 0xEE}, 2},                          // Power control
    {0xC5, {0x0E}, 1},                                // Power control
    {0x20, {0}, 0},                                   // Don't invert display
    //{0x21, {0}, 0}, //Invert colors
    //*/
    {0, {0}, 0xff}};

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
static void lcd_spi_pre_transfer_callback(spi_transaction_t *t) {
    gpio_set_level(ST7735_AO_GPIO, (int)t->user);
}

void st7735_Init() {
    gpio_pad_select_gpio(ST7735_RESET_GPIO);
    gpio_pad_select_gpio(ST7735_AO_GPIO);
    gpio_pad_select_gpio(ST7735_CS_GPIO);
    gpio_set_direction(ST7735_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(ST7735_AO_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(ST7735_CS_GPIO, GPIO_MODE_OUTPUT);

#ifdef ST7735_BACKLIGHT_GPIO
    gpio_pad_select_gpio(ST7735_BACKLIGHT_GPIO);
    gpio_set_direction(ST7735_BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(ST7735_BACKLIGHT_GPIO, 0);
#endif

    gpio_set_level(ST7735_AO_GPIO, 0);

    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = ST7735_MOSI_GPIO,
        .sclk_io_num = ST7735_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(dmaBuffer)  // 16 lines * 128 pixel + hd
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,       //Clock out  Hz
        .mode = 0,                                //SPI mode 0
        .spics_io_num = ST7735_CS_GPIO,           //CS pin
        .queue_size = 1,                          //one transaction at a time (single dmaBuffer)
        .pre_cb = lcd_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };

    ret = spi_bus_initialize(ST7735_SPI_HOST, &buscfg, ST7735_DMA_CHANNEL);
    ESP_ERROR_CHECK(ret);
    //Attach the LCD to the SPI bus
    ret = spi_bus_add_device(ST7735_SPI_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    // reset LCD
    gpio_set_level(ST7735_RESET_GPIO, 0);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    gpio_set_level(ST7735_RESET_GPIO, 1);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    printf("\nLCD reset finish. RST:%d CS:%d SCLK:%d MOSI:%d", ST7735_RESET_GPIO, ST7735_CS_GPIO, ST7735_CLK_GPIO, ST7735_MOSI_GPIO);

    //Send all the commands
    for (int cmd = 0; lcd_init_cmds[cmd].databytes != 0xff; cmd++) {
        lcd_cmd(lcd_init_cmds[cmd].cmd);
        lcd_data(lcd_init_cmds[cmd].data, lcd_init_cmds[cmd].databytes & 0x1F);
        if (lcd_init_cmds[cmd].databytes & 0xF0) {
            uint32_t dt = (lcd_init_cmds[cmd].databytes & 0xF0) >> 4;
            vTaskDelay(dt * 20 / portTICK_RATE_MS);
        }
    }
    printf("\nLCD inited\n");
}

static void st7735_setWindow(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy) {
    uint8_t data[4];
    data[0] = data[2] = 0;
    data[1] = 2 + x;
    data[3] = x + dx + 1;
    lcd_cmd(ST7735_CASET);  // Column addr set: XSTART, XEND
    lcd_data(data, 4);
    data[1] = 1 + y;
    data[3] = y + dy;
    lcd_cmd(ST7735_RASET);  // Row addr set: XSTART, XEND
    lcd_data(data, 4);
    //printf("\nLCD window %d %d %d %d\n", x, y, dx, dy);
}

void st7735_Clear(unsigned short color) {
    st7735_RectangleFill(0, 0, CONFIG_TFT_WIDTH, CONFIG_TFT_HEIGHT, color);
}

void st7735_BackLight(unsigned char percentPWD) {
}

uint8_t st7735_Text(uint8_t x0, uint8_t y0, const char *str, unsigned short fColor, unsigned short bColor) {
    int bPtr = 0;
    int x = x0, dy = 0;
    for (; dy < 16 && (y0 + dy) < CONFIG_TFT_HEIGHT; dy++) {
        x = x0;
        for (int p = 0; str[p] != 0; p++) {
            uint8_t charRow = ascii_8x16[((str[p] - 0x20) * 16) + dy];
            uint8_t mask = 0x80;
            for (int i = 0; i < 8 && x < CONFIG_TFT_WIDTH; i++, x++) {
                unsigned short color = (charRow & mask) != 0 ? fColor : bColor;
                mask = mask >> 1;
                dmaBuffer[bPtr++] = color >> 8;
                dmaBuffer[bPtr++] = color;
            }
        }
    }
    int dx = x - x0;
    //printf("\nLCD text %d %d %d %d '%s'", x0, y0, dx, dy, str);
    st7735_setWindow(x0, y0, dx, dy);
    lcd_cmd(ST7735_RAMWR);
    lcd_data4dmaBuf(dx * dy * 2);
    return dx;
}

void st7735_RectangleFill(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, unsigned short color) {
    int dl = sizeof(dmaBuffer) / (dx * 2);
    for (int l = 0; l < dy; l += dl) {
        int ddy = (dy - l) < dl ? dy - l : dl;
        st7735_setWindow(x, y + l, dx, ddy);
        int bytes = dx * ddy * 2;
        for (int i = 0; i < bytes;) {
            dmaBuffer[i++] = color >> 8;
            dmaBuffer[i++] = color;
        }
        lcd_cmd(ST7735_RAMWR);
        lcd_data4dmaBuf(bytes);
    }
}

void st7735_Rectangle(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, unsigned short color) {
    st7735_RectangleFill(x, y, dx, 1, color);
    st7735_RectangleFill(x, y, 1, dy, color);
    st7735_RectangleFill(x + dx - 1, y, 1, dy, color);
    st7735_RectangleFill(x, y + dy - 1, dx, 1, color);
}

void st7735_Img(uint8_t x, uint8_t y, uint8_t dx, uint8_t dy, const unsigned char *map) {
    int n = 0;
    int dl = sizeof(dmaBuffer) / (dx * 2);
    for (int l = 0; l < dy; l += dl) {
        int ddy = (dy - l) < dl ? dy - l : dl;
        st7735_setWindow(x, y + l, dx, ddy);
        int bytes = dx * ddy * 2;
        for (int i = 0; i < bytes;) {
            dmaBuffer[i++] = map[n++];
            dmaBuffer[i++] = map[n++];
        }
        lcd_cmd(ST7735_RAMWR);
        lcd_data4dmaBuf(bytes);
    }
}
