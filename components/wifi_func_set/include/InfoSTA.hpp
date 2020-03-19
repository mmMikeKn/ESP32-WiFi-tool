#ifndef _InfoSTA_HPP
#define _InfoSTA_HPP

#include "common.hpp"

class InfoSTA {
   public:
    InfoSTA(uint8_t *src_addr, uint8_t *dst_addr, uint8_t *bssid_addr,
        uint32_t ts_sec, uint16_t seq_num, bool isDataRq) {
        memcpy(mac, src_addr, 6);
        if (dst_addr != NULL)
            memcpy(dest, dst_addr, 6);
        else
            memset(dest, 0, 6);
        if (bssid_addr != NULL)
            memcpy(bssid, bssid_addr, 6);
        else
            memset(bssid, 0, 6);
        isActive = true;
        wasConnected = isDataRq;
        last_ts_sec = ts_sec;
        sequence_number = seq_num;
    }

    bool equal(uint8_t *addr, int sz = 6) {
        return memcmp(addr, mac, sz) == 0;
    }

    void updateStateByTime(uint32_t ts_sec) {
        absent_dt_sec = ts_sec - last_ts_sec;
        if (absent_dt_sec > max_ts_sec_offline) isActive = false;
    }

    void update(uint32_t ts_sec, uint8_t *dst_addr, uint8_t *bssid_addr,
                uint16_t seq_num, bool isDataRq) {
        if (isDataRq) wasConnected = true;
        if (dst_addr != NULL) memcpy(dest, dst_addr, 6);
        if (bssid_addr != NULL) memcpy(bssid, bssid_addr, 6);
        last_ts_sec = ts_sec;
        sequence_number = seq_num;
        isActive = true;
    }

    std::string to_json() {
        std::stringstream ss;
        ss << "{\"connect\":" << (wasConnected ? "true " : "false");
        ss << ", \"active\":\"" << (isActive ? "true " : "false") << "\"";
        ss << ", \"ts\":" << last_ts_sec;
        ss << ", \"mac\":\"" << bin2mac(mac).c_str() << "\"";
        if (wasConnected) {
            ss << ", \"dest\":\"" << bin2mac(dest).c_str() << "\"";
            ss << ", \"bssid\":\"" << bin2mac(bssid).c_str() << "\"";
        }
        ss << "}";
        return ss.str();
    }

    std::string toString() {
        std::stringstream ss;
        ss << absent_dt_sec << " " << bin2mac(mac).c_str();
        return ss.str();
    }

   public:
    bool isActive, wasConnected;
    uint8_t mac[6], dest[6], bssid[6];
    uint16_t sequence_number;

   private:
    const uint32_t static max_ts_sec_offline = 60;
    uint32_t last_ts_sec, absent_dt_sec;

   private:
};

#endif  //_InfoSTA_HPP