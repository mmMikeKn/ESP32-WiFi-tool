#ifndef _WIFI_SNIFFER_H
#define _WIFI_SNIFFER_H

#include <string>
#include <vector>
#include "PcapFileQueue.hpp"
#include "esp_wifi.h"

class WifiSniffer {
   public:
    WifiSniffer(int channel = 1);
    ~WifiSniffer();

    void setFilter(std::vector<std::string> &list);
    void setQueue(PcapFileQueue *queue);

    bool start();
    void stop();
    void changeChannel(int channel) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    struct statistics_t {
        int cnt_management_frame;
        int cnt_data_frame;
        int cnt_authentication_frame;
        int cnt_deauthentication_frame;
        int cnt_association_frame;
        int cnt_disassociation_frame;
        struct {
            uint8_t src_mac[6], dst_mac[6];
            int8_t rssi;
            int8_t noise_floor;  // noise floor of Radio Frequency Module(RF). unit: 0.25dBm
            uint32_t time;
        } deauth;
    };
    void getStatistics(statistics_t &s);
    std::string getMacListJson();
    void getMacLists(std::vector<std::string> &listOnline,
                     std::vector<std::string> &listOffline,
                     std::vector<std::string> &listProbe);
    void sendDeauth();

   private:
    int channel;
    uint8_t *filter, filter_sz;
    const static bool listen_as_AP = true;  // deauth  can be send in AP mode
};

#endif  //_WIFI_SNIFFER_H