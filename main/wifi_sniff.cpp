#include "esp32/rom/crc.h"

#include "WifiSniffer.hpp"
#include "icon_maps.h"
#include "menu_semaphore.h"
#include "wifi_func.hpp"

//static const char *TAG = "wifi-sniff";

enum SNIFFER_MODE {
    PCAP_FILE,
    MONITOR_MAC,
    DO_DEAUTH,
    SCAN_DEAUTH
};

static void wifi_Sniffer(SNIFFER_MODE mode, std::vector<std::string> &filter, char *hd) {
    switch (mode) {
        case SNIFFER_MODE::PCAP_FILE:
            drawHeader(save_icon16x16, hd);
            break;
        case SNIFFER_MODE::MONITOR_MAC:
            drawHeader(user_group_icon16x16, hd);
            break;
        case SNIFFER_MODE::DO_DEAUTH:
            drawHeader(thumbs_down_icon16x16, hd);
            break;
        case SNIFFER_MODE::SCAN_DEAUTH:
            drawHeader(red_pin_icon16x16, hd);
            break;
    }
    int channel = 1;
    WifiSniffer wifiSniffer(channel);
    wifiSniffer.setFilter(filter);
    PcapFileQueue *queue;

    if (mode == SNIFFER_MODE::PCAP_FILE) {
        queue = new PcapFileQueue(1024 * 32);
        int st = queue->createPcapFile(FNAME_WIFI_PCUP, PCAP_LINKTYPE_IEEE802_11);
        if (st != 0) {
            scr_error((st == PcapFileQueue::ERR_OPEN_FILE)
                          ? "Failed to open file '" FNAME_WIFI_PCUP "' for writing"
                          : "Failed to write file '" FNAME_WIFI_PCUP "'");
            return;
        }
        wifiSniffer.setQueue(queue);
    }

    if (!wifiSniffer.start()) {
        return;
    }

    uint32_t last_deauth_time = esp_timer_get_time();
    do {
        if (blink(10)) {
            WifiSniffer::statistics_t s;
            wifiSniffer.getStatistics(s);
            scr_printf(1, 16 * 1, SCR_INFO_COLOR, "%2d:%d %d      ", channel, s.cnt_management_frame, s.cnt_data_frame);
            if (mode == SNIFFER_MODE::SCAN_DEAUTH) {
                int32_t dt = (esp_timer_get_time() - last_deauth_time) / 1000;
                scr_printf(2, 16 * 2, SCR_DUMP_COLOR, "tm: %d.%03d ", dt / 1000, dt % 1000);
                if (s.cnt_deauthentication_frame > 0) {
                    last_deauth_time = s.deauth.time;
                    scr_printf(2, 16 * 3, SCR_WRN_COLOR, ">" MACSTR, MAC2STR(s.deauth.src_mac));
                    scr_printf(2, 16 * 4, SCR_WRN_COLOR, "<" MACSTR, MAC2STR(s.deauth.dst_mac));
                    scr_printf(2, 16 * 5, SCR_WRN_COLOR, "rssi: %d  ", s.deauth.rssi);
                    scr_printf(2, 16 * 6, SCR_WRN_COLOR, "noise f: %d  ", s.deauth.noise_floor);
                    scr_printf(1, 16 * 7, SCR_INFO_COLOR, "DEAUTHENT:%4d ", s.cnt_deauthentication_frame);
                }
            } else if (mode == SNIFFER_MODE::PCAP_FILE) {
                int x = queue->getSize() * (CONFIG_TFT_WIDTH - 2) / queue->getMaxSize();
                st7735_RectangleFill(x + 1, 16 * 1 - 2, CONFIG_TFT_WIDTH - 1 - x, 2, ST7735_Green);
                if (x > 0) st7735_RectangleFill(1, 16 * 1 - 2, x, 2, ST7735_Red);
                int frames = s.cnt_management_frame + s.cnt_data_frame;
                if (frames) {
                    int n = queue->getLostDataCnt();
                    //int p = n * 1000L / (s.management_frame + s.data_frame);
                    if (n != 0) scr_printf(1, 16 * 2, SCR_WRN_COLOR, "LOST: %d", n);
                    //if (n != 0) scr_printf(1, 16 * 2, SCR_WRN_COLOR, "%d/%2d.%1d%% ", n, p / 100, p % 10);
                }
                //scr_printf(16 + 16 * 2, 0, SCR_INFO_COLOR, "%6d Kb", fsize / 1024);
                scr_printf(1, 16 * 3, SCR_INFO_COLOR, "AUTHENTIC:%4d ", s.cnt_authentication_frame);
                scr_printf(1, 16 * 4, SCR_INFO_COLOR, "DEAUTHENT:%4d ", s.cnt_deauthentication_frame);
                scr_printf(1, 16 * 5, SCR_INFO_COLOR, "ASSOCIATI:%4d ", s.cnt_association_frame);
                scr_printf(1, 16 * 6, SCR_INFO_COLOR, "DISASSOCI:%4d ", s.cnt_disassociation_frame);
                scr_printf(1, 16 * 7, SCR_INFO_COLOR, "%6d Kb", queue->getPcapFileBodySize() / 1024);
            } else if (mode == SNIFFER_MODE::MONITOR_MAC || mode == DO_DEAUTH) {
                std::vector<std::string> listOnline, listOffline, listProbe;
                wifiSniffer.getMacLists(listOnline, listOffline, listProbe);
                scr_printf(3, 16 * 2, ST7735_Green, ST7735_Black, "On%02d", listOnline.size());
                scr_printf(3 + 8 * 4 + 5, 16 * 2, ST7735_Yellow, ST7735_Black, "Off%02d", listOffline.size());
                scr_printf(3 + 8 * 4 + 5 + 8 * 5 + 3, 16 * 2, ST7735_DarkGrey, ST7735_Black, "Pr%03d", listProbe.size());
                int i = 0;
                for (int j = 0; i < 5 && j < listOnline.size(); i++, j++) {
                    scr_printf(1, 16 * (3 + i), ST7735_Green, ST7735_Black, listOnline.at(j).c_str());
                }
                for (int j = 0; i < 6 && j < listOffline.size(); i++, j++) {
                    scr_printf(1, 16 * (3 + i), ST7735_Yellow, ST7735_Black, listOffline.at(j).c_str());
                }
                for (int j = 0; i < 6 && j < listProbe.size(); i++, j++) {
                    scr_printf(1, 16 * (3 + i), ST7735_DarkGrey, ST7735_Black, listProbe.at(j).c_str());
                }
            }
            //wifiSniffer.changeChannel();
            //if((++channel) > 13) channel = 1; //US = 11, EU = 13, Japan = 14
            if (mode == DO_DEAUTH) wifiSniffer.sendDeauth();
        }
        if (mode == SNIFFER_MODE::PCAP_FILE) queue->savePcapFileAllCurrentChunks();
    } while (getKeyPressed(10 / portTICK_RATE_MS) != GPIO_KEY_CANCEL);

    if (mode == SNIFFER_MODE::PCAP_FILE) {
        queue->isPutDisabled = true;
        vTaskDelay(800 / portTICK_RATE_MS);
        queue->savePcapFileAllCurrentChunks();
        if (queue->getPcapFileCRC32() != queue->getPutDataCRC32()) {
            scr_error("%u (%d) %u (%d)",
                      queue->getPcapFileCRC32(),
                      queue->getPcapFileBodySize(),
                      queue->getPutDataCRC32(),
                      queue->getPutDataFullSize());
        }
        wifiSniffer.setQueue(NULL);
        delete queue;
    }
    wifiSniffer.stop();
    //-----------
    FILE *f = fopen(FNAME_WIFI_SNIFFED_MAC_JSON, "w");
    if (f == NULL) {
        scr_error("Failed to open file " FNAME_WIFI_SNIFFED_MAC_JSON " for writing");
    } else {
        fputs(wifiSniffer.getMacListJson().c_str(), f);
        fclose(f);
    }
    //-----------
    xSemaphoreGive(menu_semaphore);
    vTaskDelete(NULL);
}

//=================================================================================================

void wifi_Sniffer_pcap_without_filter(void *arg) {
    std::vector<std::string> filter;
    wifi_Sniffer(PCAP_FILE, filter, (char *)arg);
}

void wifi_Sniffer_pcap_with_filter(void *arg) {
    std::vector<std::string> filter;
    loadList(FNAME_WIFI_FILTER_MAC_TXT, filter);
    wifi_Sniffer(PCAP_FILE, filter, (char *)arg);
}

void wifi_Sniffer_mac_without_filter(void *arg) {
    std::vector<std::string> filter;
    wifi_Sniffer(MONITOR_MAC, filter, (char *)arg);
}

void wifi_Sniffer_mac_with_filter(void *arg) {
    std::vector<std::string> filter;
    loadList(FNAME_WIFI_FILTER_MAC_TXT, filter);
    wifi_Sniffer(MONITOR_MAC, filter, (char *)arg);
}

void wifi_Sniffer_deauth_all(void *arg) {
    std::vector<std::string> filter;
    wifi_Sniffer(DO_DEAUTH, filter, (char *)arg);
}

void wifi_Sniffer_deauth_with_filter(void *arg) {
    std::vector<std::string> filter;
    loadList(FNAME_WIFI_FILTER_MAC_TXT, filter);
    wifi_Sniffer(DO_DEAUTH, filter, (char *)arg);
}

void wifi_Sniffer_deauth_scanner(void *arg) {
    std::vector<std::string> filter;
    wifi_Sniffer(SCAN_DEAUTH, filter, (char *)arg);
}

void wifi_Sniffer_deauth_scanner_filter(void *arg) {
    std::vector<std::string> filter;
    loadList(FNAME_WIFI_FILTER_MAC_TXT, filter);
    wifi_Sniffer(SCAN_DEAUTH, filter, (char *)arg);
}
