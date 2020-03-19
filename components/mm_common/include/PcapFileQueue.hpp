#ifndef _BytesQueue_HPP
#define _BytesQueue_HPP

#include "esp_system.h"
#include "freertos/FreeRTOS.h"

//IEEE 802.11 (wifi)
#define PCAP_LINKTYPE_IEEE802_11 105

/* https://tools.ietf.org/html/rfc2516  
                  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                  |       DESTINATION_ADDR        |
                  |          (6 octets)           |
                  |                               |
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                  |         SOURCE_ADDR           |
                  |          (6 octets)           |
                  |                               |
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                  |    ETHER_TYPE  (2 octets)     |
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                  ~                               ~
                  ~           payload             ~
                  ~                               ~
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                  |           CHECKSUM            |
                  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
#define PCAP_LINKTYPE_ETHERNET 1 

class PcapFileQueue {
   public:
    PcapFileQueue() { init(DEF_SIZE); };
    PcapFileQueue(int init_sz) { init(init_sz); }
    ~PcapFileQueue() {
        if (buf != NULL) free(buf);
        if (pcap_file_ptr != NULL) fclose(pcap_file_ptr);
    }

    int getMaxSize() { return max_sz; }
    int getFreeSize() { return max_sz - sz; }
    int getSize() { return sz; }
    bool putPCAP(uint8_t *ptr, uint16_t length);

    uint32_t getPutDataCRC32() { return put_data_crc32; }
    uint32_t getPutDataFullSize() { return put_data_sz; }
    uint32_t getLostDataCnt() { return lost_data_cnt; }

    int createPcapFile(const char *fname, int networkType);
    int savePcapFileChunk();
    bool savePcapFileAllCurrentChunks();

    uint32_t getPcapFileBodySize() { return pcap_file_body_sz; }
    uint32_t getPcapFileCRC32() { return pcap_file_body_crc32; }

   public:
    bool isPutDisabled;

    static const int MAX_SIZE = 1024 * 64;
    static const int MIN_SIZE = 1024 * 16;
    static const int DEF_SIZE = 1024 * 32;

    static const int ERR_OPEN_FILE = -1;
    static const int ERR_WRITE_FILE = -2;
    static const int ERR_NO_FILE = -3;

   private:
    void init(int init_sz);
    int get(uint8_t *ptr, uint16_t length);
    bool put(uint8_t *ptr, uint16_t length);

   private:
    portMUX_TYPE mutex;
    //--
    uint8_t *buf;
    int32_t sz, in_ptr, out_ptr, lost_data_cnt, max_sz;
    //--
    uint32_t put_data_crc32, put_data_sz;
    //--
    FILE *pcap_file_ptr;
    uint32_t pcap_file_body_sz, pcap_file_body_crc32;
};

#endif  // _BytesQueue_HPP