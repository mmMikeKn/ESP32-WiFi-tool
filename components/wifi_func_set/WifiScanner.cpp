#include <string.h>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

#include "esp_log.h"

#include "WifiScanner.hpp"
#include "common.hpp"

static const char *TAG = "SCAN_WIFI";
static bool _isScanDone = true;
//static std::mutex _scannerResultMutext;

class WiFiAPInfo {
   public:
    WiFiAPInfo(wifi_ap_record_t &r) {
        count = 1;
        memcpy(bssid, r.bssid, sizeof(bssid));
        memcpy(ssid, r.ssid, sizeof(ssid));
        rssi = r.rssi;
        authmode = r.authmode;
        pairwise_cipher = r.pairwise_cipher;
        group_cipher = r.group_cipher;
        primary = r.primary;
    }

    bool equal(const wifi_ap_record_t &r) {
        return memcmp(r.bssid, bssid, sizeof(bssid)) == 0 &&
               memcmp(r.ssid, ssid, sizeof(ssid)) == 0;
    }

    std::string to_json() {
        std::stringstream ss;
        ss << "{\"cnt\":" << count;
        ss << ",\"ssid\":\"" << ssid << "\"";
        ss << ",\"rssi\":" << rssi / count;
        ss << ",\"ch\":" << (int)primary;
        ss << ",\"auth\":\"";
        switch (authmode) {
            case WIFI_AUTH_OPEN:
                ss << "OPEN";
                break;
            case WIFI_AUTH_WEP:
                ss << "WEP";
                break;
            case WIFI_AUTH_WPA_PSK:
                ss << "WPA_PSK";
                break;
            case WIFI_AUTH_WPA2_PSK:
                ss << "WPA2_PSK";
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                ss << "WPA_WPA2_PSK";
                break;
            case WIFI_AUTH_WPA2_ENTERPRISE:
                ss << "WPA2_ENTERPRISE";
                break;
            case WIFI_AUTH_WPA3_PSK:
                ss << "WPA3_PSK";
                break;
            case WIFI_AUTH_MAX:
                ss << "MAX";
                break;
            default:
                ss << "undefined";
        }
        ss << "\"";
        if (authmode != WIFI_AUTH_WEP) {
            ss << ",\"pChiper\":\"";
            fillChiper(pairwise_cipher, ss);
            ss << "\",\"gChiper\":\"";
            fillChiper(group_cipher, ss);
            ss << "\"";
        }
        ss << ",\"bssid\":\"" << std::hex << std::setw(2) << std::setfill('0');
        for (int i = 0; i < 6; i++) ss << (int)bssid[i];
        ss << "\"}";
        return ss.str();
    }

    void inc_count() { count++; }
    void add_over_rssi(int next_rssi) { rssi += next_rssi; }
    wifi_auth_mode_t get_auth_mode() { return authmode; }
    int get_count() { return count; }
    uint8_t *get_ssid() { return ssid; }

   private:
    void fillChiper(wifi_cipher_type_t type, std::stringstream &ss) {
        switch (type) {
            case WIFI_CIPHER_TYPE_NONE:
                ss << "NONE";
                break;
            case WIFI_CIPHER_TYPE_WEP40:
                ss << "WEP40";
                break;
            case WIFI_CIPHER_TYPE_WEP104:
                ss << "WEP104";
                break;
            case WIFI_CIPHER_TYPE_TKIP:
                ss << "TKIP";
                break;
            case WIFI_CIPHER_TYPE_CCMP:
                ss << "CCMP";
                break;
            case WIFI_CIPHER_TYPE_TKIP_CCMP:
                ss << "TKIP_CCMP";
                break;
            default:
                ss << "undefined";
        }
    }

   private:
    int count;
    uint8_t bssid[6];                             /**< MAC address of AP */
    uint8_t ssid[sizeof(wifi_ap_record_t::ssid)]; /**< SSID of AP */
    int rssi;                                     /**< signal strength of AP */
    wifi_auth_mode_t authmode;                    /**< authmode of AP */
    wifi_cipher_type_t pairwise_cipher;           /**< pairwise cipher of AP */
    wifi_cipher_type_t group_cipher;              /**< group cipher of AP */
    uint8_t primary;                              /**< channel of AP */
};

static std::vector<std::unique_ptr<WiFiAPInfo>> _scannerResult;
static int _scanCount;

static std::string to_json() {
    std::stringstream ss;
    ss << "{ \"scan_count\":" << _scanCount << ", \"ap_list\":[" << std::endl;
    for (auto it = _scannerResult.begin(); it != _scannerResult.end(); it++) {
        if (it != _scannerResult.begin()) ss << std::endl
                                             << ",";
        ss << it->get()->to_json();
    }
    ss << "]}";
    return ss.str();
}

static void scan_done_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    uint16_t sta_number = 0;
    wifi_ap_record_t *list;

    ESP_LOGD(TAG, "+++ scan_done_handler");
    esp_wifi_scan_get_ap_num(&sta_number);
    if (sta_number == 0) {
        ESP_LOGI(TAG, "empty list (esp_wifi_scan_get_ap_num)");
        _isScanDone = true;
        return;
    }
    list = (wifi_ap_record_t *)malloc(sta_number * sizeof(wifi_ap_record_t));
    if (list == NULL) {
        scr_error("wifi_ap_record malloc() fail [sz:%d]", sta_number);
        return;
    }

    ERROR_CHECK_RET(esp_wifi_scan_get_ap_records(&sta_number, (wifi_ap_record_t *)list));
    //_scannerResultMutext.lock();
    for (int i = 0; i < sta_number; i++) {
        bool newRecord = true;
        for (auto it = _scannerResult.begin(); it != _scannerResult.end(); it++) {
            if (it->get()->equal(list[i])) {
                newRecord = false;
                it->get()->inc_count();
                it->get()->add_over_rssi(list[i].rssi);
                break;
            }
        }
        if (newRecord) {
            _scannerResult.push_back(std::unique_ptr<WiFiAPInfo>(new WiFiAPInfo(list[i])));
        }
    }
    //_scannerResultMutext.unlock();

    free(list);
    ESP_LOGD(TAG, "--- scan_done_handler (%d)", _scannerResult.size());
    ESP_LOGV(TAG, "--> %s", to_json().c_str());

    _scanCount++;
    _isScanDone = true;
}

bool WifiScanner::isScanDone() {
    return _isScanDone;
}

bool WifiScanner::open() {
    ESP_LOGD(TAG, "+++ open");
    _scannerResult.clear();
    _scanCount = 0;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ERROR_CHECK_RET_BOOL(esp_wifi_init(&cfg));

    ERROR_CHECK_RET_BOOL(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &scan_done_handler, NULL));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_STA));

    ERROR_CHECK_RET_BOOL(esp_wifi_start());
    ESP_LOGD(TAG, "--- open");
    return true;
}

bool WifiScanner::startScanner() {
    ESP_LOGD(TAG, "+++ startScanner");
    wifi_scan_config_t scanCfg;
    scanCfg.ssid = NULL;
    scanCfg.bssid = NULL;
    scanCfg.channel = 0;
    scanCfg.show_hidden = true;

    if (isPassiveMode) {
        scanCfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scanCfg.scan_time.passive = maxScanTimePerChannel;
    } else {
        scanCfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scanCfg.scan_time.active.min = 100;
        scanCfg.scan_time.active.max = maxScanTimePerChannel;
    }
    _isScanDone = false;
    ERROR_CHECK_RET_BOOL(esp_wifi_scan_start(&scanCfg, false));
    ESP_LOGD(TAG, "--- startScanner");
    return true;
}

bool WifiScanner::haltScanner() {
    ESP_LOGD(TAG, "+++ haltScanner");
    if (!_isScanDone) esp_wifi_scan_stop();
    ESP_LOGD(TAG, "--- haltScanner");
    return true;
}

void WifiScanner::close() {
    ESP_LOGD(TAG, "+++ close");
    _scannerResult.clear();
    _scanCount = 0;
    esp_wifi_stop();
    ESP_LOGD(TAG, "--- close");
}

std::string WifiScanner::getJson() {
    return to_json();
}

std::vector<std::string> WifiScanner::getApList() {
    std::vector<std::string> list;
    for (auto it = _scannerResult.begin(); it != _scannerResult.end(); it++) {
        list.push_back(std::string((char *)it->get()->get_ssid()));
    }
    return list;
}

void WifiScanner::getStatistics(WifiScanner::statistics_t *s) {
    memset(s, 0, sizeof(WifiScanner::statistics_t));
    s->count = _scanCount;
    s->ap_num = _scannerResult.size();
    for (auto it = _scannerResult.begin(); it != _scannerResult.end(); it++) {
        if (it->get()->get_auth_mode() == WIFI_AUTH_OPEN) s->auth_open_num++;
        if (it->get()->get_auth_mode() == WIFI_AUTH_WEP) s->auth_wep_num++;
        if (it->get()->get_count() == _scanCount) s->strong_ap_num++;
        if (it->get()->get_ssid()[0] == 0) continue;

        int n = 1;
        uint8_t ssid[sizeof(wifi_ap_record_t::ssid)];
        memcpy(ssid, it->get()->get_ssid(), sizeof(ssid));
        for (auto it1 = _scannerResult.begin(); it1 != _scannerResult.end(); it1++) {
            if (it1 != it && strncmp((char *)it1->get()->get_ssid(), (char *)ssid, sizeof(ssid)) == 0) {
                n++;
            }
        }
        for (int i = 0; i < SORT_LIST_SIZE; i++) {
            if (s->sort_ssid_count[i] == n && strncmp((char *)s->sort_ssid[i], (char *)ssid, sizeof(ssid)) == 0) break;
            if (s->sort_ssid_count[i] < n) {
                s->sort_ssid_count[i] = n;
                memcpy(s->sort_ssid[i], ssid, sizeof(ssid));
                break;
            }
        }
    }
}
