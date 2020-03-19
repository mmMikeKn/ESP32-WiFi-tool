#ifndef _WIFI_FUNC_H
#define _WIFI_FUNC_H

#include <string>
#include <vector>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "common.hpp"

//wifi_sniff.cpp
void wifi_Sniffer_pcap_without_filter(void *arg);
void wifi_Sniffer_pcap_with_filter(void *arg);
void wifi_Sniffer_mac_without_filter(void *arg);
void wifi_Sniffer_mac_with_filter(void *arg);
void wifi_Sniffer_deauth_all(void *arg);
void wifi_Sniffer_deauth_with_filter(void *arg);
void wifi_Sniffer_deauth_scanner(void *arg);
void wifi_Sniffer_deauth_scanner_filter(void *arg);

//wifi_ap_scan.cpp
void wifi_ApScan(void *arg);

//wifi_beacon.cpp
void wifi_BeaconSpam_fromFile(void *arg);
void wifi_BeaconSpam_savedAP(void *arg);

//wifi_route.cpp
void wifi_route_all(void *arg);
void wifi_route_with_filter(void *arg);
void wifi_route_all_sniff(void *arg);
void wifi_route_with_filter_sniff(void *arg);

#endif  // _WIFI_FUNC_H