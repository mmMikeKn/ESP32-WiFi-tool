#include <stdio.h>
#include <string.h>

#include "WifiScanner.hpp"
#include "icon_maps.h"
#include "menu_semaphore.h"
#include "wifi_func.hpp"

//static const char *TAG = "wifi-ap-scan";

void wifi_ApScan(void *arg) {
    drawHeader(search_icon16x16, (char *)arg);

    WifiScanner wifiScaner(false, 200);
    if (!wifiScaner.open()) return;
    while (true) {
        wifiScaner.startScanner();
        while (!wifiScaner.isScanDone()) {
            blink(1);
            vTaskDelay(100 / portTICK_RATE_MS);
        }
        WifiScanner::statistics_t s;
        wifiScaner.getStatistics(&s);

        scr_printf(1, 16 * 1, SCR_INFO_COLOR, "%d AP: %d/%d    ", s.count, s.ap_num, s.strong_ap_num);
        if (s.auth_wep_num != 0 || s.auth_open_num != 0) {
            scr_printf(1, 16 * 2, ST7735_Green, ST7735_Black, "WEP:%2d OPEN:%2d  ", s.auth_wep_num, s.auth_open_num);
        }
        for (int i = 0; i < WifiScanner::SORT_LIST_SIZE; i++) {
            if (s.sort_ssid_count[i] != 0)
                scr_printf(1, 16 * (3 + i), SCR_DUMP_COLOR, "%1d %s                ", s.sort_ssid_count[i], s.sort_ssid[i]);
        }
        if (getKeyPressed(500 / portTICK_RATE_MS) == GPIO_KEY_CANCEL) {
            wifiScaner.haltScanner();
            break;
        }
    }
    //-----------
    FILE *f = fopen(FNAME_SCAN_AP_RESULT_JSON, "w");
    if (f == NULL) {
        scr_error("Failed to open file " FNAME_SCAN_AP_RESULT_JSON " for writing");
        return;
    }
    fputs(wifiScaner.getJson().c_str(), f);
    fclose(f);
    //-----------
    f = fopen(FNAME_SCAN_AP_RESULT_TXT, "w");
    if (f == NULL) {
        scr_error("Failed to open file " FNAME_SCAN_AP_RESULT_TXT " for writing");
        return;
    }
    std::vector<std::string> list = wifiScaner.getApList();
    for (auto it = list.begin(); it != list.end(); it++) {
        if (it != list.begin()) fputc('\n', f);
        fputs(it->c_str(), f);
    }
    fclose(f);
    //-----------
    xSemaphoreGive(menu_semaphore);
    vTaskDelete(NULL);
}

