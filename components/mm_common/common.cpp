#include "common.hpp"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <iomanip>
#include <sstream>

#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "common";

std::string bin2hex(void *data, int size) {
    const unsigned char *src = (const unsigned char *)data;
    std::stringstream o;
    for (int i = 0; i < size; i++)
        o << std::hex << std::setw(2) << std::uppercase << std::setfill('0') << (unsigned int)src[i];
    return o.str();
}

std::string bin2mac(uint8_t mac[]) {
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::uppercase << std::setfill('0');
    ss << (unsigned int)mac[0] << ":" << (unsigned int)mac[1] << ":" << (unsigned int)mac[2] << ":";
    ss << (unsigned int)mac[3] << ":" << (unsigned int)mac[4] << ":" << (unsigned int)mac[5];
    return ss.str();
}

int hex2bin(const char *mac, uint8_t *dst, int max_size) {
    int n_sz = 0, max_n_sz = max_size * 2;
    uint8_t b = 0;
    while (n_sz < max_n_sz) {
        char c = *mac;
        mac++;
        if (c == ':') continue;
        uint8_t n;
        if (c >= '0' && c <= '9')
            n = c - '0';
        else if (c >= 'A' && c <= 'F')
            n = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            n = c - 'a' + 10;
        else
            break;
        if ((n_sz & 1) == 0)
            b = n << 4;
        else
            dst[n_sz / 2] = b | n;
        n_sz++;
    }
    return n_sz / 2;
}

void scr_error(const char *format, ...) {
    // font 8x16
    char str[(CONFIG_TFT_WIDTH / 8) * (CONFIG_TFT_HEIGHT / 16) + 1];
    va_list args;
    va_start(args, format);
    vsnprintf(str, sizeof(str) - 1, format, args);
    va_end(args);
    st7735_Clear(ST7735_Black);
    int sz = strlen(str);
    for (int i = 0, j = 0, y = 0; i < sz; i++, j++) {
        char tmp[(CONFIG_TFT_WIDTH / 8) + 1];
        tmp[j] = str[i];
        if (str[i] == '\n' || j >= (CONFIG_TFT_WIDTH / 8) || i == (sz - 1)) {
            tmp[j] = 0;
            st7735_Text(0, y, tmp, ST7735_Red, ST7735_Black);
            j = -1;
            y += 16;
            if (str[i] != '\n' && i != (sz - 1)) i--;
        }
    }
    ESP_LOGE(TAG, "scr_error:%s\n", str);
    for (int i = 0; i < 3; i++)
        if (getKeyPressed(10000 / portTICK_RATE_MS) == GPIO_KEY_CANCEL) break;
    st7735_Clear(ST7735_Black);
}

void scr_printf(uint8_t x, uint8_t y, unsigned short fColor, unsigned short bkColor, const char *format, ...) {
    char str[(CONFIG_TFT_WIDTH / 8) + 1];
    va_list args;
    va_start(args, format);
    vsnprintf(str, sizeof(str) - 1, format, args);
    va_end(args);
    st7735_Text(x, y, str, fColor, bkColor);
}

static xQueueHandle keyboard_queue = NULL;

static int key_list[] = {GPIO_KEY_CANCEL, GPIO_KEY_ENTER, GPIO_KEY_UP, GPIO_KEY_DOWN};
static void keyboard_scan(void *arg) {
    while (true) {
        vTaskDelay(200 / portTICK_RATE_MS);
        for (int i = 0; i < 4; i++)
            if (gpio_get_level((gpio_num_t)key_list[i]) == 0) {
                //printf("gpio:%d\n", key_list[i]);
                xQueueSend(keyboard_queue, &key_list[i], 0);
            }
    }
}

int getKeyPressed(int dt) {
    int key_num;
    if (xQueueReceive(keyboard_queue, &key_num, dt)) return (int)key_num;
    return -1;
}

void initKeyBoard() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = ((1ULL << GPIO_KEY_CANCEL) | (1ULL << GPIO_KEY_ENTER) | (1ULL << GPIO_KEY_UP) | (1ULL << GPIO_KEY_DOWN));
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    keyboard_queue = xQueueCreate(5, sizeof(int));
    xTaskCreate(keyboard_scan, "keyboard_scan", 2048, NULL, 10, NULL);
}

bool blink(int divider) {
    static bool blink = true;
    static int cnt = 0;

    cnt++;
    if ((cnt % divider) == 0) {
        st7735_RectangleFill(0, 4, 4, 16 - 8, blink ? ST7735_Green : ST7735_Black);
        blink = !blink;
        return true;
    }
    return false;
}

void drawHeader(const unsigned char *icon, char *hd) {
    st7735_Clear(ST7735_Black);
    st7735_Img(8, 0, 16, 16, icon);
    st7735_Text(8 + 16 + 2, 0, hd, SCR_HD_COLOR);
}

void loadList(const char *fname, std::vector<std::string> &list) {
    char buf[512];
    int n;
    std::string body = "";
    FILE *fd = fopen(fname, "r");
    if (fd) {
        while ((n = fread(buf, 1, sizeof(buf), fd)) > 0) body.append(buf, n);
        fclose(fd);
    }
    std::string token;
    std::istringstream ts(body);
    while (std::getline(ts, token, '\n')) {
        list.push_back(token);
    }
 }
