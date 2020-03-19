#ifndef _WIFI_BEACON_H
#define _WIFI_BEACON_H

#include <string>
#include <vector>
#include "esp_wifi.h"

class WifiBeacon {
   public:
    WifiBeacon(std::vector<std::string> beaconList) : beaconList(beaconList){};
    ~WifiBeacon() { close(); };

    bool open();
    bool sendNextBeacon();
    void close();

    unsigned int getCurrentBacon() { return currentBeacon; }

   private:
    std::vector<std::string> beaconList;
    unsigned int currentBeacon = 0;
};

#endif  //_WIFI_BEACON_H