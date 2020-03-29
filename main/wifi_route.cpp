#include <string.h>
#include "esp_wifi.h"
#include "lwip/ip.h"
#include "lwip/netif.h"

#include "freertos/event_groups.h"

#include "common.hpp"
#include "icon_maps.h"
#include "menu_semaphore.h"
#include "netif_callbacks.hpp"
#include "wifi_func.hpp"
#include "wifi_pkt.h"

#define SCR_LINE_TRAFFIC 1
#define SCR_LINE_DST_STA_MAC 2
#define SCR_LINE_DST_STA_IP 3
#define SCR_LINE_SRC_STA_MAC 4
#define SCR_LINE_SRC_STA_IP 5
#define SCR_LINE_SRC_STA_HOSTNAME 6
#define SCR_LINE_WARN_LOST 7
#define SCR_LINE_WARN 7

static const char *TAG = "wifi-route";
static int deauth_pkg_cnt = 0;
static uint8_t esp32_ap_mac[6], src_sta_mac[6];
static uint8_t *mac_filter, mac_filter_sz;

#define WIFI_EVENT_AP_START_BIT BIT0
static EventGroupHandle_t router_event_group;

typedef struct {
    char ap_ssid[33], ap_pswd[70];
    char ap_mac[6 * 3 + 1];
    char sta_ssid[33], sta_pswd[70];
    char sta_hostname[50];
    char sta_mac[6 * 3 + 1];
} ROUTER_CFG;

//------------------------------------------------------

static bool setConfValue(char *val, int max_sz, std::vector<std::string> &config,
                         int p, const char *def) {
    max_sz--;
    if (config.size() <= p || config[p].length() == 0) {
        if (def[0] != 0) {
            strncpy(val, def, max_sz);
            val[max_sz] = 0;
            ESP_LOGI(TAG, "router save default value[%d/%d]: [%s]", p, config.size(), val);
            return true;
        }
        return false;
    }
    strncpy(val, config[p].c_str(), max_sz);
    val[max_sz] = 0;
    return false;
}

static void loadConfig(ROUTER_CFG &cfg) {
    std::vector<std::string> config;
    loadList(FNAME_WIFI_ROUTER_TXT, config);
    memset(&cfg, 0, sizeof(cfg));
    bool setDefault = setConfValue(cfg.ap_ssid, sizeof(cfg.ap_ssid), config, 0, "RESP");
    setDefault |= setConfValue(cfg.ap_pswd, sizeof(cfg.ap_pswd), config, 1, "");
    setDefault |= setConfValue(cfg.ap_mac, sizeof(cfg.ap_mac), config, 2, "");
    setDefault |= setConfValue(cfg.sta_ssid, sizeof(cfg.sta_ssid), config, 3, "Test123");
    setDefault |= setConfValue(cfg.sta_pswd, sizeof(cfg.sta_pswd), config, 4, "qwert123");
    setDefault |= setConfValue(cfg.sta_hostname, sizeof(cfg.sta_hostname), config, 5, "phone0");
    setDefault |= setConfValue(cfg.sta_mac, sizeof(cfg.sta_mac), config, 6, "");

    setDefault = false;

    char tmp[sizeof(ROUTER_CFG) + 10];
    sprintf(tmp, "%s\n%s\n%s\n%s\n%s\n%s", cfg.ap_ssid, cfg.ap_pswd, cfg.ap_mac,
            cfg.sta_ssid, cfg.sta_pswd, cfg.sta_hostname);
    ESP_LOGI(TAG, "router config file: [%s]", tmp);
    if (setDefault) {
        FILE *f = fopen(FNAME_WIFI_ROUTER_TXT, "w");
        if (f == NULL) {
            scr_error("Failed to open file " FNAME_WIFI_ROUTER_TXT " for writing");
        } else {
            fputs(tmp, f);
            fclose(f);
        }
    }
}

static void IRAM_ATTR wifi_sniffer_cb(void *recv_buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *spkt = (wifi_promiscuous_pkt_t *)recv_buf;
    if (spkt->rx_ctrl.rx_state != 0 || type == WIFI_PKT_MISC)
        return;
    if (memcmp(esp32_ap_mac, ((wifi_common_body_t *)(spkt->payload))->address1, 6) == 0 || memcmp(esp32_ap_mac, ((wifi_common_body_t *)(spkt->payload))->address2, 6) == 0) {
        if ((((wifi_frame_ctrl_t *)(spkt->payload))->type == 0 &&
             (((wifi_frame_ctrl_t *)(spkt->payload))->subtype == 0xC || ((wifi_frame_ctrl_t *)(spkt->payload))->subtype == 0xA))) {
            deauth_pkg_cnt++;
            ESP_LOGI(TAG, "wifi deauth t:%x st:%x " MACSTR " -> " MACSTR,
                     ((wifi_frame_ctrl_t *)(spkt->payload))->type,
                     ((wifi_frame_ctrl_t *)(spkt->payload))->subtype,
                     MAC2STR(((wifi_common_body_t *)(spkt->payload))->address2),
                     MAC2STR(((wifi_common_body_t *)(spkt->payload))->address1));
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "STA got ip: " IPSTR, IP2STR(&event->ip_info.ip));
            scr_printf(0, 16 * SCR_LINE_DST_STA_IP, ST7735_Green, ST7735_Black, IPSTR, IP2STR(&event->ip_info.ip));
            return;
        }
        case IP_EVENT_AP_STAIPASSIGNED: {
            ESP_LOGI(TAG, "set ip: " IPSTR, IP2STR(&(((ip_event_ap_staipassigned_t *)event_data)->ip)));
            scr_printf(0, 16 * SCR_LINE_SRC_STA_IP, ST7735_Green, ST7735_Black, IPSTR, IP2STR(&(((ip_event_ap_staipassigned_t *)event_data)->ip)));
            return;
        }
    }
    ESP_LOGI(TAG, "ip_event_handler:%d", event_id);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "WIFI_EVENT_SCAN_DONE");
            return;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
            xEventGroupSetBits(router_event_group, WIFI_EVENT_AP_START_BIT);
            return;
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
            return;
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            ERROR_CHECK_RET(esp_wifi_connect());
            return;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
            return;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED " MACSTR, MAC2STR(event->bssid));
            scr_printf(0, 16 * SCR_LINE_DST_STA_MAC, ST7735_Green, ST7735_Black, MACSTR, MAC2STR(event->bssid));
            return;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED " MACSTR, MAC2STR(event->bssid));
            scr_printf(0, 16 * SCR_LINE_DST_STA_MAC, ST7735_Red, ST7735_Black, MACSTR, MAC2STR(event->bssid));
            scr_printf(0, 16 * SCR_LINE_DST_STA_IP, ST7735_Red, ST7735_Black, "....           ");
            ERROR_CHECK_RET(esp_wifi_connect());
            return;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            bool isFiltered = false;
            if (mac_filter != NULL) {
                for (int i = 0; i < mac_filter_sz; i++) {
                    if (memcmp(mac_filter + (i * 7) + 1, event->mac, mac_filter[i * 7]) == 0) {
                        isFiltered = true;
                        break;
                    }
                }
            }
            memcpy(src_sta_mac, event->mac, 6);
            if (mac_filter != NULL && !isFiltered) {
                ESP_LOGI(TAG, "STA " MACSTR " join and deauth (filter), AID=%d", MAC2STR(event->mac), event->aid);
                scr_printf(0, 16 * SCR_LINE_SRC_STA_MAC, ST7735_Yellow, ST7735_Black, MACSTR, MAC2STR(event->mac));
                esp_wifi_deauth_sta(event->aid);
            } else {
                ESP_LOGI(TAG, "STA " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
                scr_printf(0, 16 * SCR_LINE_SRC_STA_MAC, ST7735_Green, ST7735_Black, MACSTR, MAC2STR(event->mac));
            }
            return;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "STA " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
            scr_printf(0, 16 * SCR_LINE_SRC_STA_MAC, ST7735_DarkGrey, ST7735_Black, MACSTR, MAC2STR(src_sta_mac));
            scr_printf(0, 16 * SCR_LINE_SRC_STA_IP, ST7735_Red, ST7735_Black, "....           ");
            scr_printf(0, 16 * SCR_LINE_SRC_STA_HOSTNAME, ST7735_Red, ST7735_Black, "....           ");
            return;
        }
    }
    ESP_LOGI(TAG, "wifi_event_handler:%d", event_id);
}

extern uint8_t pached_softAP_mac[];  // patched mac_addr.c

static bool init_wifi(ROUTER_CFG &cfg) {
    // ----------- set AP/STA MAC ---------------
    uint8_t tmp[6];
    for (int i = 0; i < 6; i++) tmp[i] = esp_random();
    tmp[0] &= 0xfE;  //Base MAC must be a unicast MAC (least significant bit of first byte must be zero).
    tmp[0] |= 0x02;  // set the "locally administered" bit (bit value 0x02 in the first byte) to avoid collisions.
    if (cfg.ap_mac[0] != 0) {
        hex2bin(cfg.ap_mac, pached_softAP_mac, 6);
    } else {
        memset(pached_softAP_mac, 0, 6);
    }
    if (cfg.sta_mac[0] != 0) hex2bin(cfg.sta_mac, tmp, 6);
    esp_base_mac_addr_set(tmp);
    // ------------- ------------
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ERROR_CHECK_RET_BOOL(esp_wifi_init(&wifi_init_config));

    ERROR_CHECK_RET_BOOL(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ERROR_CHECK_RET_BOOL(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL));

    ERROR_CHECK_RET_BOOL(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_LOGI(TAG, "WIFI_MODE_APSTA mode active");
    // ------------- ------------
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, cfg.sta_ssid);
    strcpy((char *)wifi_config.sta.password, cfg.sta_pswd);
    ERROR_CHECK_RET_BOOL(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "WIFI_IF_STA configured");
    // ------------- ------------
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.ap.channel = 1;
    wifi_config.ap.authmode = cfg.ap_pswd[0] == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.ssid_hidden = 0;
    wifi_config.ap.max_connection = 5;
    strcpy((char *)wifi_config.ap.ssid, cfg.ap_ssid);
    strcpy((char *)wifi_config.ap.password, cfg.ap_pswd);
    wifi_config.ap.beacon_interval = 100;
    ERROR_CHECK_RET_BOOL(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_LOGI(TAG, "WIFI_IF_AP configured");
    // ------------- ------------
    dhcps_offer_t dhcps_dns_value = OFFER_DNS;
    dhcps_set_option_info(6, &dhcps_dns_value, sizeof(dhcps_dns_value));

    ip_addr_t dnsserver;
    dnsserver.u_addr.ip4.addr = htonl(0x08080808);
    dnsserver.type = IPADDR_TYPE_V4;
    dhcps_dns_setserver(&dnsserver);
    ESP_LOGI(TAG, "dhcps configured");
    // ------------- ------------
    ERROR_CHECK_RET_BOOL(esp_wifi_start());
    ESP_LOGI(TAG, "wifi started");

    // ------------- ------------
    ESP_LOGI(TAG, "Wait AP start");
    xEventGroupWaitBits(router_event_group, WIFI_EVENT_AP_START_BIT, 0, 1, 3000 / portTICK_RATE_MS);
    if (netif_default == NULL) {
        scr_error("NO default netif");
        return false;
    }

    netif_default->napt = 1;
    ESP_LOGI(TAG, "NAT enabled for '%c%c%d'", netif_default->name[0], netif_default->name[1], netif_default->num);

    // ------------- ------------
    struct netif *netif;
    NETIF_FOREACH(netif) {
        if (netif->name[0] == 's' && netif->name[1] == 't') {
            netif->hostname = cfg.sta_hostname;
            ESP_LOGI(TAG, "set hostname='%s' for '%c%c%d'", netif->hostname, netif->name[0], netif->name[1], netif->num);
        }
    }

    // ------------- ------------
    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous(true));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_filter(&filter));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_channel(wifi_config.ap.channel, WIFI_SECOND_CHAN_NONE));
    // ------------- ------------

    ERROR_CHECK_RET_BOOL(esp_wifi_get_mac(WIFI_IF_AP, esp32_ap_mac));
    ESP_LOGI(TAG, "ESP32 AP. mac=" MACSTR " ssid=[%s] password=[%s]", MAC2STR(esp32_ap_mac), cfg.ap_ssid, cfg.ap_pswd);
    ERROR_CHECK_RET_BOOL(esp_wifi_get_mac(WIFI_IF_STA, tmp));
    ESP_LOGI(TAG, "ESP32 STA. mac=" MACSTR " ssid=[%s] password=[%s] hostname=[%s]", MAC2STR(tmp), cfg.sta_ssid, cfg.sta_pswd, cfg.sta_hostname);

    return true;
}

//============================================================================
static void wifi_route(void *arg, bool useFilter, bool savePCAP) {
    deauth_pkg_cnt = 0;
    memset(src_sta_mac, 0, sizeof(src_sta_mac));

    router_event_group = xEventGroupCreate();
    drawHeader(savePCAP ? save_icon16x16 : router_icon16x16, (char *)arg);
    ROUTER_CFG cfg;
    loadConfig(cfg);
    if (!init_wifi(cfg)) return;

    //--------
    mac_filter_sz = 0;
    mac_filter = NULL;
    if (useFilter) {
        std::vector<std::string> list;
        loadList(FNAME_WIFI_FILTER_MAC_TXT, list);
        mac_filter_sz = list.size();
        mac_filter = (uint8_t *)malloc(7 * mac_filter_sz);
        for (int i = 0; i < mac_filter_sz; i++) mac_filter[i * 7] = hex2bin(list.at(i).c_str(), mac_filter + i * 7 + 1, 6);
        for (int i = 0; i < mac_filter_sz; i++) {
            ESP_LOGI(TAG, "filter[%d] '%s'", i, bin2hex(mac_filter + i * 7 + 1, mac_filter[i * 7]).c_str());
        }
    }
    //-------

    PcapFileQueue *queue;
    if (savePCAP) {
        queue = new PcapFileQueue(1024 * 64);
        int st = queue->createPcapFile(FNAME_IP_PCUP, PCAP_LINKTYPE_ETHERNET);
        if (st != 0) {
            scr_error((st == PcapFileQueue::ERR_OPEN_FILE)
                          ? "Failed to open file '" FNAME_IP_PCUP "' for writing"
                          : "Failed to write file '" FNAME_IP_PCUP "'");
            return;
        }
        netif_callbacks_set_queue(queue);
    }
    //==================================
    netif_callbacks_set();
    do {
        if (blink(50)) {
            char hostname[20];
            if (netif_callbacks_get_hostname(src_sta_mac, hostname, sizeof(hostname))[0] != 0)
                scr_printf(0, 16 * SCR_LINE_SRC_STA_HOSTNAME, ST7735_Green, ST7735_Black, "%s               ", hostname);
            else
                scr_printf(0, 16 * SCR_LINE_SRC_STA_HOSTNAME, ST7735_Red, ST7735_Black, "...                ");

            if (deauth_pkg_cnt > 0) {
                scr_printf(0, 16 * SCR_LINE_WARN, ST7735_Red, ST7735_Black, "Deauth PKG:%d", deauth_pkg_cnt);
            }
            if (savePCAP) {
                int x = queue->getSize() * (CONFIG_TFT_WIDTH - 2) / queue->getMaxSize();
                st7735_RectangleFill(x + 1, 16 * 1 - 2, CONFIG_TFT_WIDTH - 1 - x, 2, ST7735_Green);
                if (x > 0) st7735_RectangleFill(1, 16 * 1 - 2, x, 2, ST7735_Red);
                if (queue->getLostDataCnt() > 0) {
                    scr_printf(1, 16 * SCR_LINE_WARN_LOST, SCR_WRN_COLOR, "LOST: %d", queue->getLostDataCnt());
                }
                scr_printf(1, 16 * SCR_LINE_TRAFFIC, SCR_INFO_COLOR, "%6d Kb", queue->getPcapFileBodySize() / 1024);
            } else {
                scr_printf(1, 16 * SCR_LINE_TRAFFIC, SCR_INFO_COLOR, ">%d <%d Kb", netif_callbacks_in_sz / 1024, netif_callbacks_out_sz / 1024);
            }
        }
        if (savePCAP) queue->savePcapFileAllCurrentChunks();

    } while (getKeyPressed(10 / portTICK_RATE_MS) != GPIO_KEY_CANCEL);

    if (savePCAP) {
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
        netif_callbacks_set_queue(NULL);
        delete queue;
    }
    std::string inf = netif_callbacks_restore();
    //---------
    ESP_LOGI(TAG, "info:%s", inf.c_str());
    FILE *f = fopen(FNAME_WIFI_ROUTER_INFO_JSON, "w");
    if (f == NULL) {
        scr_error("Failed to open file " FNAME_WIFI_ROUTER_INFO_JSON " for writing");
    } else {
        fputs(inf.c_str(), f);
        fclose(f);
    }

    //==================================
    esp_wifi_set_promiscuous(false);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ip_event_handler);
    esp_wifi_stop();
    if (mac_filter != NULL) {
        free(mac_filter);
        mac_filter = NULL;
    }
    //-----------
    vEventGroupDelete(router_event_group);
    xSemaphoreGive(menu_semaphore);
    vTaskDelete(NULL);
}

//----------------------------------
void wifi_route_all(void *arg) {
    wifi_route(arg, false, false);
}

void wifi_route_with_filter(void *arg) {
    wifi_route(arg, true, false);
}

void wifi_route_all_sniff(void *arg) {
    wifi_route(arg, false, true);
}

void wifi_route_with_filter_sniff(void *arg) {
    wifi_route(arg, true, true);
}
