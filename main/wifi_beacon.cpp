#include <stdio.h>
#include <string.h>

#include "WifiBeacon.hpp"
#include "icon_maps.h"
#include "common.hpp"
#include "menu_semaphore.h"
#include "wifi_func.hpp"

static const char *TAG = "wifi-beacon";

static const char *def_list[] = {
    "phone777",
    "IGOR12",
    "shareKK",
    "samsung19"
    "HOST-noname",
    "nemo-ze",
    "hyper-T",
    "my-phone",
    "trefs",
    "Info-common",
    "zero-1",
    ""  // mast be
};

static void wifi_BeaconSpam(bool useScannedAPs, char *hd) {
    drawHeader(wifi_beacon_icon16x16, hd);
    std::vector<std::string> beaconList;
    const char *fname = useScannedAPs ? FNAME_SCAN_AP_RESULT_TXT : FNAME_WIFI_BACKON_SPAM;

    ESP_LOGI(TAG, "wifi_BeaconSpam. load from '%s'", fname);
    loadList(fname, beaconList);

    if (beaconList.size() == 0) {
        FILE *f = fopen(FNAME_WIFI_BACKON_SPAM, "w");
        for (int i = 0; def_list[i][0] != 0; i++) {
            ESP_LOGI(TAG, "add default: %s", def_list[i]);
            beaconList.push_back(std::string(def_list[i]));
            if (f != NULL) {
                fputs(def_list[i], f);
                fputc('\n', f);
            }
        }
        if (f != NULL) fclose(f);
    }
    scr_printf(1, 16 * 1, SCR_INFO_COLOR, "LIST SIZE: %d", beaconList.size());

    WifiBeacon wifiBeacon(beaconList);
    if (!wifiBeacon.open()) return;
    while (true) {
        if (blink(20)) {
            int w = (wifiBeacon.getCurrentBacon() / 6) * 6;
            for (int i = 0; i < 6 && w < beaconList.size(); i++, w++) {
                scr_printf(1, 16 * (2 + i), SCR_DUMP_COLOR, "%s                  ", beaconList.at(w).c_str());
            }
        }
        wifiBeacon.sendNextBeacon();
        int dt = 100 / beaconList.size();
        if (dt < 10) dt = 10;
        if (getKeyPressed(dt / portTICK_RATE_MS) == GPIO_KEY_CANCEL) {
            break;
        }
    }
    xSemaphoreGive(menu_semaphore);
    vTaskDelete(NULL);
}

void wifi_BeaconSpam_fromFile(void *arg) {
    wifi_BeaconSpam(false, (char *)arg);
}

void wifi_BeaconSpam_savedAP(void *arg) {
    wifi_BeaconSpam(true, (char *)arg);
}
