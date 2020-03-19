#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"

extern "C" {
#include "esp_private/wifi.h"
#include "esp_wifi_default.h"
}

#include "web_host.hpp"
#include "wifi_func.hpp"

extern bool init_SD();
extern bool init_netif(std::string &ifkey);

#define NVS_STORAGE_MENUPOS_NAME "menupos"

static const char *TAG = "menu";
//------------------------------------------------------------------------------------------
xQueueHandle menu_semaphore;

//------------------------------------------------------------------------------------------
typedef void (*menu_func_t)(void *);
typedef struct MENU {
    char item[16];
    char description[16 * 3 + 1];
    menu_func_t func;
    struct MENU *tree;
} MENU_t;

static MENU_t wifi_beacons_menu[] = {
    {"AP scanner", "Scann and save\nAP list", wifi_ApScan, NULL},
    {"Beacon spam AP", "Beacon spam froms\ncanned AP list", wifi_BeaconSpam_savedAP, NULL},
    {"Beacon spam fl", "Beacon spam from\n" FNAME_WIFI_BACKON_SPAM, wifi_BeaconSpam_fromFile, NULL},
    {}};

static MENU_t wifi_sniffer_menu[] = {
    {"PCAP file", "Save all wifi\n" FNAME_WIFI_PCUP, wifi_Sniffer_pcap_without_filter, NULL},
    {"PCAP filter", "filter:" FNAME_WIFI_FILTER_MAC_TXT "\n" FNAME_WIFI_PCUP, wifi_Sniffer_pcap_with_filter, NULL},
    {"Sniff MAC", "Sniff all wifi\n" FNAME_WIFI_SNIFFED_MAC_JSON, wifi_Sniffer_mac_without_filter, NULL},
    {"S.MAC filtered", "f:" FNAME_WIFI_FILTER_MAC_TXT "\n" FNAME_WIFI_SNIFFED_MAC_JSON, wifi_Sniffer_mac_with_filter, NULL},
    {}};

static MENU_t wifi_deauth_menu[] = {
    {"Deauth scan", "Deauth source\nscanner", wifi_Sniffer_deauth_scanner, NULL},
    {"Deauth scan f", "Deauth source\nf:" FNAME_WIFI_FILTER_MAC_TXT, wifi_Sniffer_deauth_scanner_filter, NULL},
    {"Deauth all", "Send deauth\nfrom all AP", wifi_Sniffer_deauth_all, NULL},
    {"Deauth filtered", "Send deauth\nf:" FNAME_WIFI_FILTER_MAC_TXT, wifi_Sniffer_deauth_with_filter, NULL},
    {}};

static MENU_t wifi_router_menu[] = {
    {"Route all", "Route all\nconnection", wifi_route_all, NULL},
    {"Route filtered", "Route filter\n" FNAME_WIFI_FILTER_MAC_TXT, wifi_route_with_filter, NULL},
    {"S.Route all", "Route+sniff\nconnection", wifi_route_all_sniff, NULL},
    {"S.Route filter", "Route+sniff f\n" FNAME_WIFI_FILTER_MAC_TXT, wifi_route_with_filter_sniff, NULL},
    {}};

static MENU_t root_menu[] = {
    {"Web control", "AP mode WebHost", wifi_AP_WebHost, NULL},
    {"Wifi Beacons", "Wifi beacons\nfunctions", NULL, wifi_beacons_menu},
    {"Wifi sniffer", "Wifi sniffer\nfunctions", NULL, wifi_sniffer_menu},
    {"Wifi deauth", "Wifi deauth\nfunctions", NULL, wifi_deauth_menu},
    {"Wifi router", "Wifi router\nfunctions", NULL, wifi_router_menu},
    {}};

#define MENU_ROWS_NUM 5
#define MENU_ROWS_START 16
static int firstItemInWin = 0;

//------------------------------------------------------------------------------------------

static void drawMenu(int &pos, const char *hd, const MENU_t *m) {
    //ESP_LOGI(TAG, "start drawMenu <%s> m[%d].item='%s'", hd, pos, m[pos].item);
    int num = 0;
    while (m[num].item[0] != 0) {
        num++;
    }
    if (pos < 0) {
        pos = num - 1;
    }
    if (pos >= num) {
        pos = 0;
    }
    if (pos == 0) {
        firstItemInWin = 0;
    }
    if ((firstItemInWin + pos) >= MENU_ROWS_NUM) {
        firstItemInWin = pos - MENU_ROWS_NUM + 1;
    }
    if (firstItemInWin > pos) {
        firstItemInWin = pos;
    }
    if (firstItemInWin < 0) {
        firstItemInWin = 0;
    }

    st7735_Clear(ST7735_Black);

    st7735_RectangleFill(0, 0, 127, 16, ST7735_DarkGrey);
    st7735_Text(5, 0, hd, ST7735_Blue2, ST7735_DarkGrey);
    st7735_RectangleFill(2, MENU_ROWS_START, 128 - 5, 4 + 16 * MENU_ROWS_NUM, ST7735_Black);
    st7735_Rectangle(2, MENU_ROWS_START, 128 - 5, 4 + 16 * MENU_ROWS_NUM, ST7735_White);
    for (int i = firstItemInWin, j = 0; i < num && j < MENU_ROWS_NUM; i++, j++) {
        st7735_Text(5, MENU_ROWS_START + 3 + j * 16, m[i].item,
                    m[i].tree == NULL ? ST7735_GreenYellow : ST7735_Blue,
                    pos == i ? ST7735_DarkGrey : ST7735_Black);
    }
    if (num > MENU_ROWS_NUM) {
        int dy = MENU_ROWS_NUM * 16 / num;
        st7735_RectangleFill(128 - 5, MENU_ROWS_START + 5, 4, dy * num, ST7735_DarkCyan);
        st7735_RectangleFill(128 - 5, MENU_ROWS_START + 5 + dy * firstItemInWin, 4, dy * MENU_ROWS_NUM, ST7735_Cyan);
    }
    st7735_RectangleFill(0, MENU_ROWS_START + 4 + 16 * MENU_ROWS_NUM, 127, 16 * 2, ST7735_DarkGrey);
    char tmp[17];
    int ofs = 16;
    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, m[pos].description, 16);
    char *ptr = strchr(tmp, '\n');
    if (ptr != NULL) {
        *ptr = 0;
        ofs = ptr - tmp + 1;
    }
    st7735_Text(0, MENU_ROWS_START + 4 + 16 * MENU_ROWS_NUM, tmp, ST7735_White, ST7735_DarkGrey);
    if (strlen(m[pos].description) > 16)
        st7735_Text(0, MENU_ROWS_START + 4 + 16 * MENU_ROWS_NUM + 15, m[pos].description + ofs, ST7735_White, ST7735_DarkGrey);
}

static void storeMunuPos(const char *hd, int32_t pos) {
    nvs_handle_t handle;
    esp_err_t err;
    if ((err = nvs_open(NVS_STORAGE_MENUPOS_NAME, NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open '" NVS_STORAGE_MENUPOS_NAME "' error %s", esp_err_to_name(err));
    } else {
        if ((err = nvs_set_i32(handle, hd, pos)) != ESP_OK)
            ESP_LOGE(TAG, "nvs_set_i32 '" NVS_STORAGE_MENUPOS_NAME "/%s [%d] error %s", hd, pos, esp_err_to_name(err));
        else
            ESP_LOGI(TAG, "nvs_set_i32 '" NVS_STORAGE_MENUPOS_NAME "/'%s' [%d]", hd, pos);
        nvs_close(handle);
    }
}

static void execMenu(std::vector<int> &menu_enter, int &pos, const char *hd, const MENU_t *m) {
    if (menu_enter.size() > 0) {
        pos = menu_enter.at(0);
        ESP_LOGI(TAG, "set stored '/%s' pos=%d", hd, pos);
        menu_enter.erase(menu_enter.begin());
        if (menu_enter.size() > 0) {
            int subPos = menu_enter.at(0);
            menu_enter.erase(menu_enter.begin());
            int num = 0;
            while (m[num].item[0] != 0)
                num++;
            if (pos < num) {  // verify...
                ESP_LOGI(TAG, "going to submenu '/%s' pos=%d", m[pos].item, subPos);
                execMenu(menu_enter, subPos, m[pos].item, m[pos].tree);
            }
        }
    }
    while (true) {
        drawMenu(pos, hd, m);
        int key;
        while ((key = getKeyPressed(10 / portTICK_RATE_MS)) < 0) {
        }
        if (key == GPIO_KEY_UP)
            pos--;
        else if (key == GPIO_KEY_DOWN)
            pos++;
        else if (key == GPIO_KEY_ENTER) {
            if (m[pos].func != NULL) {
                storeMunuPos(hd, pos);
                menu_semaphore = xSemaphoreCreateBinary();
                xTaskCreatePinnedToCore(m[pos].func, m[pos].item, 8192, (void *)m[pos].item, 10, NULL, 1);
                while (xSemaphoreTake(menu_semaphore, 10000 / portTICK_RATE_MS) != pdTRUE) {
                }
                //ESP_LOGI(TAG, "menu func exit pos:%d\n", pos);
            } else {
                int subPos = 0;
                storeMunuPos(hd, pos);
                execMenu(menu_enter, subPos, m[pos].item, m[pos].tree);
            }
        } else if (key == GPIO_KEY_CANCEL) {
            //ESP_LOGI(TAG, "exit from execMenu");
            return;
        }
    }
}

void readMenuEnter(nvs_handle_t &handle, std::vector<int> &menu_enter, char *hd, MENU_t *m) {
    esp_err_t err;
    int32_t pos = 0;
    if ((err = nvs_get_i32(handle, hd, &pos)) != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGI(TAG, "not found '" NVS_STORAGE_MENUPOS_NAME "'/'%s'", hd);
        else
            ESP_LOGI(TAG, "reading '" NVS_STORAGE_MENUPOS_NAME "'/'%s' error %s", hd, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "reading '" NVS_STORAGE_MENUPOS_NAME "'/%s value %d", hd, pos);
    int num = 0;
    while (m[num].item[0] != 0)
        num++;
    if (pos >= num) {
        ESP_LOGE(TAG, "reading '" NVS_STORAGE_MENUPOS_NAME "'/%s value %d >= menu size[%d]", hd, pos, num);
        return;
    }
    menu_enter.push_back(pos);
    if (m[pos].tree != NULL)
        readMenuEnter(handle, menu_enter, m[pos].item, m[pos].tree);
}

//------------------------------------------------------------------------------------------
extern "C" void app_main(void) {
    //    esp_wifi_internal_set_log_level(WIFI_LOG_VERBOSE);
    //    esp_wifi_internal_set_log_mod(WIFI_LOG_MODULE_ALL, WIFI_LOG_SUBMODULE_ALL, true);
    st7735_Init();
    st7735_Clear(ST7735_Black);
    init_SD();
    std::string ifkey;
    init_netif(ifkey);
    initKeyBoard();

    ESP_LOGI(TAG, "ifkey: '%s'", ifkey.c_str());

    int32_t pos = 0;
    char hd[sizeof(MENU_t::item)] = "root menu";
    // ------------------- load last menu position -----------
    nvs_handle_t handle;
    esp_err_t err;
    std::vector<int> menu_enter;
    if ((err = nvs_open(NVS_STORAGE_MENUPOS_NAME, NVS_READONLY, &handle)) != ESP_OK) {
        ESP_LOGI(TAG, "nvs_open '" NVS_STORAGE_MENUPOS_NAME "' error %s", esp_err_to_name(err));
    } else {
        readMenuEnter(handle, menu_enter, hd, root_menu);
        nvs_close(handle);
    }
    nvs_close(handle);
    // ------------------- show menu -----------
    while (true)
        execMenu(menu_enter, pos, hd, root_menu);
}
