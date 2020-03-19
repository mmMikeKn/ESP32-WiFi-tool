#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "WifiBeacon.hpp"
#include "common.hpp"
#include "wifi_pkt.h"

static const char *TAG = "SCAN_BEACON";

bool WifiBeacon::open() {
    ESP_LOGI(TAG, "+++ open");
    currentBeacon = 0;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ERROR_CHECK_RET_BOOL(esp_wifi_init(&cfg));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    ap_config.ap.channel = 1,
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK,
    ap_config.ap.ssid_hidden = 1,
    ap_config.ap.max_connection = 1,
    strcpy((char *)ap_config.ap.password, "undefined");
    ap_config.ap.beacon_interval = 60000;
    ERROR_CHECK_RET_BOOL(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ERROR_CHECK_RET_BOOL(esp_wifi_start());
    ERROR_CHECK_RET_BOOL(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "--- open");
    return true;
}

bool WifiBeacon::sendNextBeacon() {
    ESP_LOGI(TAG, "+++ sendNextBeacon [%d]'%s'", currentBeacon, beaconList.at(currentBeacon).c_str());

    wifi_beacon_body_t pk;
    memset(&pk, 0, sizeof(pk));
    pk.frame_ctrl.subtype = 8;                               // beacon sybtype
    memset(pk.dest_address, 0xff, sizeof(pk.dest_address));  // broadcast
    for (int i = 0; i < sizeof(pk.src_address); i++) pk.src_address[i] = pk.bssid[i] = esp_random();

    //pk.timestamp = ignore
    //pk.seq_ctrl = 0;
    pk.beacon_interval = 100;  // default = 100
    pk.capability_info = 0x0411;
    int ofs = 0;
    //------- SSID
    wifi_var_item_t *item = (wifi_var_item_t *)pk.var_list;
    item->id = 0;
    std::string ssid = beaconList.at(currentBeacon);
    item->len = ssid.size();
    memcpy(item->data, ssid.c_str(), item->len);
    ofs += sizeof(wifi_var_item_t) + item->len;
    //-------  Supported rates
    const uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    item = (wifi_var_item_t *)(pk.var_list + ofs);
    item->id = 1;
    item->len = sizeof(rates);
    memcpy(item->data, rates, item->len);
    ofs += sizeof(wifi_var_item_t) + item->len;
    //--------- DS parameters
    item = (wifi_var_item_t *)(pk.var_list + ofs);
    item->id = 3;
    item->len = 1;
    item->data[0] = 1;
    ofs += sizeof(wifi_var_item_t) + item->len;
    //--------- Traffic Indication Map
    const uint8_t tim[] = {0x01, 0x02, 0x00, 0x00};
    item = (wifi_var_item_t *)(pk.var_list + ofs);
    item->id = 5;
    item->len = sizeof(tim);
    memcpy(item->data, rates, item->len);
    ofs += sizeof(wifi_var_item_t) + item->len;
    // --------------
    int sz = sizeof(pk) - sizeof(wifi_beacon_body_t::var_list) + ofs;

    ERROR_CHECK_RET_BOOL(esp_wifi_80211_tx(WIFI_IF_AP, &pk, sz, false));

    currentBeacon++;
    if (currentBeacon >= beaconList.size()) currentBeacon = 0;
    ESP_LOGD(TAG, "--- sendNextBeacon");
    return true;
}

void WifiBeacon::close() {
    ESP_LOGD(TAG, "+++ close");
    esp_wifi_stop();
    ESP_LOGD(TAG, "--- close");
}
