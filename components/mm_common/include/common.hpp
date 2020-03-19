#ifndef _COMMON_H
#define _COMMON_H 

#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tft_st7735.h"

#define GPIO_KEY_CANCEL 27
#define GPIO_KEY_ENTER 35 
#define GPIO_KEY_UP 25
#define GPIO_KEY_DOWN 26

void initKeyBoard();
int getKeyPressed(int dt);

void scr_error(const char* format, ...);
void scr_printf(uint8_t x, uint8_t y, unsigned short fColor, unsigned short bkColor, const char *format, ...);

#define ERROR_CHECK_RET_BOOL(x) do { \
        esp_err_t __err_rc = (x);  \
        if (__err_rc != ESP_OK) { \
         scr_error("code:0x%x (%s) %s:%d '%s'", __err_rc, esp_err_to_name(__err_rc), __FILE__, __LINE__, __ASSERT_FUNC, #x); \
            return false; \
        } \
    } while(false)

#define ERROR_CHECK_RET(x) do { \
        esp_err_t __err_rc = (x);  \
        if (__err_rc != ESP_OK) { \
         scr_error("code:0x%x (%s) %s:%d '%s'", __err_rc, esp_err_to_name(__err_rc), __FILE__, __LINE__, __ASSERT_FUNC, #x); \
            return; \
        } \
    } while(false)

std::string bin2hex(void *data, int size);
std::string bin2mac(uint8_t mac[]);
int hex2bin(const char *mac, uint8_t *dst, int max_size);

bool blink(int divider);
void drawHeader(const unsigned char *icon, char *hd);
void loadList(const char *fname, std::vector<std::string> &list);

//------------------------------------------------------
#define SCR_HD_COLOR ST7735_Blue, ST7735_Black
#define SCR_INFO_COLOR ST7735_White, ST7735_Black
#define SCR_WRN_COLOR ST7735_Red, ST7735_Black
#define SCR_DUMP_COLOR ST7735_Magenta, ST7735_Black
#define SCR_DUMP_COLOR2 ST7735_Cyan, ST7735_White
//------------------------------------------------------

//------------------------------------------------------
#define SD_MOUNT_DMA_SZ 8192
#define SD_MOUNT_PATH "/sd"

#define FNAME_WIFI_PCUP SD_MOUNT_PATH"/wifi.pcp"
#define FNAME_IP_PCUP SD_MOUNT_PATH"/ip.pcp"

#define FNAME_SCAN_AP_RESULT_JSON SD_MOUNT_PATH"/scan_ap.jsn"
#define FNAME_SCAN_AP_RESULT_TXT SD_MOUNT_PATH"/scan_ap.lst"

#define FNAME_WIFI_BACKON_SPAM SD_MOUNT_PATH"/wspam.lst"

#define FNAME_WIFI_SNIFFED_MAC_JSON SD_MOUNT_PATH"/mac.jsn"
#define FNAME_WIFI_FILTER_MAC_TXT SD_MOUNT_PATH"/fmac.lst"

#define FNAME_WIFI_ROUTER_TXT SD_MOUNT_PATH"/router.txt"
#define FNAME_WIFI_ROUTER_INFO_JSON SD_MOUNT_PATH"/router.jsn"
//------------------------------------------------------

#endif // _COMMON_H