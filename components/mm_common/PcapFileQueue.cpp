#include "esp32/rom/crc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "common.hpp"

#include "PcapFileQueue.hpp"

//#define TRACE_QUEUE
static const char *TAG = "QUEUE_PCAP";
//----------------------------------------------------------
struct pcap_hdr_t {
    uint32_t magic_number;  /* magic number */
    uint16_t version_major; /* major version number */
    uint16_t version_minor; /* minor version number */
    int32_t thiszone;       /* GMT to local correction */
    uint32_t sigfigs;       /* accuracy of timestamps */
    uint32_t snaplen;       /* max length of captured packets, in octets */
    uint32_t network;       /* data link type */
};

typedef struct {
    uint32_t ts_sec;   /* timestamp seconds */
    uint32_t ts_usec;  /* timestamp microseconds */
    uint32_t incl_len; /* number of octets of packet saved in file */
    uint32_t orig_len; /* actual length of packet */
} pcaprec_hdr_t;

//----------------------------------------------------------
int PcapFileQueue::createPcapFile(const char *fname, int networkType) {
    pcap_hdr_t hd;
    hd.magic_number = 0xA1B2C3D4;  // Big-Endian
    hd.version_major = 0x02;
    hd.version_minor = 0x04;
    hd.thiszone = 0;  // GMT
    hd.sigfigs = 0;
    hd.snaplen = 65535;
    hd.network = networkType;
    if ((pcap_file_ptr = fopen(fname, "wb")) == NULL) return ERR_OPEN_FILE;
    if (fwrite(&hd, sizeof(hd), 1, pcap_file_ptr) != 1) {
        fclose(pcap_file_ptr);
        pcap_file_ptr = NULL;
        return ERR_WRITE_FILE;
    }
    fflush(pcap_file_ptr);
    pcap_file_body_crc32 = pcap_file_body_sz = 0;
    return 0;
}

int PcapFileQueue::savePcapFileChunk() {
    if (pcap_file_ptr == NULL) return ERR_NO_FILE;
    int sz;
    uint8_t buf[1024];
    if ((sz = get(buf, sizeof(buf))) > 0) {
        pcap_file_body_crc32 = crc32_le(pcap_file_body_crc32, buf, sz);
        if (fwrite(buf, sizeof(uint8_t), sz, pcap_file_ptr) != sz) {
            fclose(pcap_file_ptr);
            pcap_file_ptr = NULL;
            return ERR_WRITE_FILE;
        }
        pcap_file_body_sz += sz;
        return sz;
    }
    return 0;
}

bool PcapFileQueue::savePcapFileAllCurrentChunks() {
    int sz;
    while ((sz = savePcapFileChunk()) > 0) {
    }
    if (sz < 0) {
        scr_error("Failed to write pcap data. [%d]", sz);
        return false;
    }
    return true;
}

//----------------------------------------------------------
void PcapFileQueue::init(int init_size) {
    // look at portMUX_INITIALIZER_UNLOCKED
    mutex.owner = portMUX_FREE_VAL;
    mutex.count = 0;
    pcap_file_ptr = NULL;

    if (init_size > MAX_SIZE) init_size = MAX_SIZE;
    if (init_size < MIN_SIZE) init_size = MIN_SIZE;
    max_sz = init_size;
    sz = in_ptr = out_ptr = lost_data_cnt = 0;
    put_data_crc32 = put_data_sz = 0;
    isPutDisabled = false;
    do {
        buf = (uint8_t *)malloc(max_sz);
        if (buf == NULL) {
            ESP_LOGE(TAG, "malloc() == NULL for cash_size:%d", max_sz);
            max_sz -= 8192;
        }
    } while (max_sz > 8192 && buf == NULL);
    if (buf == NULL) max_sz = 0;
}

bool PcapFileQueue::put(uint8_t *ptr, uint16_t length) {
    if ((sz + length + 1) >= max_sz) {
#ifdef TRACE_QUEUE
        ESP_LOGE(TAG, "lost pcaprec: %d/%d/%d", length, sz, max_sz);
#endif
        lost_data_cnt++;
        return false;
    }

#ifdef TRACE_QUEUE
    ESP_LOGI(TAG, "p< [%d] L:%d (%d)  %d/%d", (int)esp_timer_get_time(), length, sz, mutex.owner, mutex.count);
#endif
    vPortCPUAcquireMutex(&mutex);
    for (int i = 0; i < length; i++) {
        if (in_ptr >= max_sz) in_ptr = 0;
        buf[in_ptr++] = ptr[i];
        sz++;
    }
    put_data_crc32 = crc32_le(put_data_crc32, ptr, length);
    put_data_sz += length;
    vPortCPUReleaseMutex(&mutex);
#ifdef TRACE_QUEUE
    ESP_LOGI(TAG, "p> [%d] (%d) %d/%d", (int)esp_timer_get_time(), sz, mutex.owner, mutex.count);
#endif
    return true;
}

int PcapFileQueue::get(uint8_t *ptr, uint16_t length) {
    int indx = 0;
#ifdef TRACE_QUEUE
    ESP_LOGI(TAG, "g< [%d] %d (%d)  %d/%d", (int)esp_timer_get_time(), length, sz, mutex.owner, mutex.count);
#endif
    vPortCPUAcquireMutex(&mutex);
    while (sz > 0 && indx < length) {
        if (out_ptr >= max_sz) out_ptr = 0;
        ptr[indx++] = buf[out_ptr++];
        sz--;
    }
    vPortCPUReleaseMutex(&mutex);
#ifdef TRACE_QUEUE
    ESP_LOGI(TAG, "g> [%d] L:%d (%d) %d/%d", (int)esp_timer_get_time(), indx, sz, mutex.owner, mutex.count);
#endif
    return indx;
}

bool PcapFileQueue::putPCAP(uint8_t *ptr, uint16_t length) {
    int pcap_rec_sz = sizeof(pcaprec_hdr_t) + length;
    if (getFreeSize() < pcap_rec_sz) {
        //ESP_LOGE(TAG, "lost pcaprec: %d/%d/%d", sz, queue_sz, queue_max_sz);
        lost_data_cnt++;
        return false;
    }
    pcaprec_hdr_t rec;
    rec.incl_len = rec.orig_len = length;
    int64_t t = esp_timer_get_time();
    rec.ts_sec = t / 1000000L;
    rec.ts_usec = t % 1000000L;
    return put((uint8_t *)&rec, sizeof(rec)) && put(ptr, length);
}