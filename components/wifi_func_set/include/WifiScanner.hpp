#ifndef _WIFI_SCANNER_H
#define _WIFI_SCANNER_H

#include <string>
#include <vector>
#include "esp_wifi.h"

class WifiScanner {
   public:
    //The values of maximum active scan time and passive scan time per channel are limited to 1500 milliseconds.
    WifiScanner(bool isPassiveMode = false, int maxScanTimePerChannel = 200) : isPassiveMode(isPassiveMode), maxScanTimePerChannel(maxScanTimePerChannel){};
    ~WifiScanner() { close(); };

    bool open();
    bool startScanner();
    bool isScanDone(void);
    bool haltScanner();
    void close();

    std::string getJson();
    std::vector<std::string> getApList();

    static const int SORT_LIST_SIZE = 5;
    struct statistics_t {
        int count, ap_num, auth_wep_num, auth_open_num, strong_ap_num;
        uint8_t sort_ssid[SORT_LIST_SIZE][sizeof(wifi_ap_record_t::ssid)];
        int sort_ssid_count[SORT_LIST_SIZE];
    };

    void getStatistics(WifiScanner::statistics_t *s);

   private:
    bool isPassiveMode;
    int maxScanTimePerChannel;
};

#endif  //_WIFI_SCANNER_H