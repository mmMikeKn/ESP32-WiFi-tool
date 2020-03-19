#include "esp_log.h"
#include "esp_system.h"
#include "lwip/netif.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/ieee.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"

#include "common.hpp"
#include "netif_callbacks.hpp"

#include <string.h>
#include <sstream>

//#define LOG_TRAFFIC

static const char *TAG = "netif-callbacks";

int netif_callbacks_in_sz, netif_callbacks_out_sz;

//-----
static PcapFileQueue *ptr_queue = NULL;
static std::vector<std::string> url_list;
static std::vector<std::string> sta_list;

static char dhcp_client_hostname[50], dhcp_client_mac[6];
static uint64_t start_time;
static netif_input_fn old_netif_input_fn = NULL;
//-----

//====================================================================================
#ifdef LOG_TRAFFIC
inline void dump(bool dir_in, struct pbuf *p, struct netif *netif) {
    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    int dlen = p->len - sizeof(struct eth_hdr);
    char hd[512];
    snprintf(hd, sizeof(hd), "%s '%c%c%d' f:%x [%d]: " MACSTR " -> " MACSTR " %04x %s..",
             dir_in ? "<<<<" : ">>>>",
             netif->name[0], netif->name[1], netif->num, (int)p->flags, (int)p->len,
             MAC2STR(ethhdr->src.addr),
             MAC2STR(ethhdr->dest.addr),
             lwip_htons(ethhdr->type),
             bin2hex(((uint8_t *)p->payload) + sizeof(struct eth_hdr), dlen > 50 ? 50 : dlen).c_str());

    dlen = 0;
    uint8_t *data = NULL;
    if (ethhdr->type == PP_HTONS(ETHTYPE_IP)) {
        struct ip_hdr *iphdr = (struct ip_hdr *)(((uint8_t *)p->payload) + sizeof(struct eth_hdr));
        int ip_len = lwip_ntohs(IPH_LEN(iphdr));
        if (IPH_PROTO(iphdr) == IP_PROTO_TCP) {
            struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + sizeof(struct eth_hdr) + IPH_HL(iphdr) * 4);
            int tcphdr_len = TCPH_HDRLEN_BYTES(tcphdr);
            data = (uint8_t *)tcphdr + tcphdr_len;
            dlen = ip_len - tcphdr_len;
            std::string hdump = bin2hex(data, dlen > 100 ? 100 : dlen);
            ESP_LOGI(TAG, "%s TCP %d.%d.%d.%d/%d->%d.%d.%d.%d/%d f:%x [%d:%d] %s",
                     hd,
                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src), lwip_htons(tcphdr->src),
                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest), lwip_htons(tcphdr->dest),
                     TCPH_FLAGS(tcphdr), ip_len, tcphdr_len, hdump.c_str());
        } else if (IPH_PROTO(iphdr) == IP_PROTO_UDP) {
            struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + sizeof(struct eth_hdr) + IPH_HL(iphdr) * 4);
            data = (u8_t *)udphdr + sizeof(struct udp_hdr);
            dlen = lwip_htons(udphdr->len);
            std::string hdump = bin2hex(data, dlen > 100 ? 100 : dlen);
            ESP_LOGI(TAG, "%s UDP %d.%d.%d.%d/%d->%d.%d.%d.%d/%d [%d]:%s",
                     hd,
                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src), lwip_htons(udphdr->src),
                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest), lwip_htons(udphdr->dest),
                     dlen, hdump.c_str());
        } else if (IPH_PROTO(iphdr) == IP_PROTO_IGMP) {
            ESP_LOGI(TAG, "%s IGMP [%d] %d.%d.%d.%d->%d.%d.%d.%d",
                     hd,
                     lwip_ntohs(IPH_LEN(iphdr)),
                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src),
                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest));
        } else if (IPH_PROTO(iphdr) == IP_PROTO_ICMP) {
            ESP_LOGI(TAG, "%s ICMP [%d]  %d.%d.%d.%d->%d.%d.%d.%d",
                     hd,
                     lwip_ntohs(IPH_LEN(iphdr)),
                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src),
                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest));
        } else {
            ESP_LOGI(TAG, "%s IP [%d] prot:%d %d.%d.%d.%d->%d.%d.%d.%d",
                     hd,
                     lwip_ntohs(IPH_LEN(iphdr)), IPH_PROTO(iphdr),
                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src),
                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest));
        }
    } else if (ethhdr->type == PP_HTONS(ETHTYPE_ARP)) {
        ESP_LOGI(TAG, "%s [ETHTYPE_ARP]", hd);
    } else if (ethhdr->type == PP_HTONS(ETHTYPE_IPV6)) {
        ESP_LOGI(TAG, "%s [ETHTYPE_IPV6]", hd);
    } else {
        ESP_LOGI(TAG, "%s", hd);
    }
    /*    
    if (data != NULL && dlen > 0) {
        std::stringstream ss;
        for (int i = 0; i < dlen && i < 100; i++) {
            char c = data[i];
            if ((c >= ' ' && c <= '}') || c == '\n')
                ss << c;
            else
                ss << '.';
        }
        ESP_LOGI(TAG, "text dump:'%s'", ss.str().c_str());
    }
*/
}
#endif  // LOG_TRAFFIC
//====================================================================================

extern "C" err_t new_netif_input_fn(struct pbuf *p, struct netif *netif) {
    netif_callbacks_in_sz += p->len;
    if (ptr_queue != NULL && !ptr_queue->isPutDisabled) {
        ptr_queue->putPCAP((uint8_t *)p->payload, p->len);
    }

    // -----------------------
    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    int dlen = p->len - sizeof(struct eth_hdr);
    if (ethhdr->type == PP_HTONS(ETHTYPE_IP)) {
        struct ip_hdr *iphdr = (struct ip_hdr *)(((uint8_t *)p->payload) + sizeof(struct eth_hdr));
        if (IPH_PROTO(iphdr) == IP_PROTO_UDP) {
            struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + sizeof(struct eth_hdr) + IPH_HL(iphdr) * 4);
            uint8_t *data = (u8_t *)udphdr + sizeof(struct udp_hdr);
            int dt = (int)(start_time - esp_timer_get_time());
            dlen = lwip_htons(udphdr->len);
            if (lwip_htons(udphdr->dest) == 67) {  // DHCPS_SERVER_PORT
                // validate DHCP options magic number 63825363
                if (dlen > 241 && data[236] == 0x63 && data[237] == 0x82 && data[238] == 0x53 && data[239] == 0x63) {
                    // FF - end of options
                    for (uint32_t i = 240; i < dlen && data[i] != 0xFF; i += data[i + 1] + 2) {
                        if (data[i] == 12) {  //DHCP_OPTION_HOSTNAME
                            uint32_t n = data[i + 1];
                            if (n >= (sizeof(dhcp_client_hostname) - 1)) n = sizeof(dhcp_client_hostname) - 1;
                            memcpy(dhcp_client_hostname, data + i + 2, n);
                            dhcp_client_hostname[n + 1] = 0;
                            memcpy(dhcp_client_mac, ethhdr->src.addr, 6);
                            ESP_LOGI(TAG, "DHCP %d.%d.%d.%d/%d->%d.%d.%d.%d/%d client hostname:'%s' src mac: " MACSTR,
                                     ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src), lwip_htons(udphdr->src),
                                     ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest), lwip_htons(udphdr->dest),
                                     dhcp_client_hostname, MAC2STR(ethhdr->src.addr));
                            const char pattern[] = "{\"mac\":\"" MACSTR "\",\"hostname\":\"%s\",\"dt\":\"%d:%02d.%03d\"}";
                            char tmp[sizeof(dhcp_client_hostname) + sizeof(pattern) + 20];
                            int dt_s = dt / 1000;
                            sprintf(tmp, pattern, MAC2STR(ethhdr->src.addr), dhcp_client_hostname, dt_s / 60, dt_s % 60, dt % 1000);
                            sta_list.push_back(std::string(tmp));
                            break;
                        }
                    }
                }
            } else if (lwip_htons(udphdr->dest) == 53) {
                char url[255];
                int ofs = 0;
                for (uint32_t i = 12; i < dlen && data[i] != 0; i += data[i] + 1) {
                    int dl = data[i];
                    if ((dl + ofs) >= (sizeof(url) - 4)) {
                        strcat(url, "...");
                        break;
                    }
                    memcpy(url + ofs, data + i + 1, dl);
                    ofs += dl;
                    url[ofs++] = '.';
                }
                if (ofs > 0) ofs--;
                url[ofs] = 0;
                ESP_LOGI(TAG, "DNS %d.%d.%d.%d/%d->%d.%d.%d.%d/%d url:'%s' src mac: " MACSTR,
                         ip4_addr1_16_val(iphdr->src), ip4_addr2_16_val(iphdr->src), ip4_addr3_16_val(iphdr->src), ip4_addr4_16_val(iphdr->src), lwip_htons(udphdr->src),
                         ip4_addr1_16_val(iphdr->dest), ip4_addr2_16_val(iphdr->dest), ip4_addr3_16_val(iphdr->dest), ip4_addr4_16_val(iphdr->dest), lwip_htons(udphdr->dest),
                         url, MAC2STR(ethhdr->src.addr));
                const char pattern[] = "{\"mac\":\"" MACSTR "\",\"url\":\"%s\",\"dt\":\"%d:%02d.%03d\"}";
                char tmp[sizeof(url) + sizeof(pattern) + 20];
                int dt_s = dt / 1000;
                sprintf(tmp, pattern, MAC2STR(ethhdr->src.addr), url, dt_s / 60, dt_s % 60, dt % 1000);
                url_list.push_back(std::string(tmp));
            }
        }
    }
    //------------------------
#ifdef LOG_TRAFFIC
    if (p->len < sizeof(struct eth_hdr)) {  // mac_s[6] + mac_d[6] + lwip_ieee_eth_type[2]
        ESP_LOGI(TAG, "<<<<< '%c%c%d' UNDEF [%d]: %s", netif->name[0], netif->name[1], netif->num, p->len, bin2hex(p->payload, p->len).c_str());
    } else {
        dump(true, p, netif);
    }
#endif  // LOG_TRAFFIC
    return old_netif_input_fn(p, netif);
}

static netif_linkoutput_fn old_netif_linkoutput_fn = NULL;
extern "C" err_t new_netif_linkoutput_fn(struct netif *netif, struct pbuf *p) {
    netif_callbacks_out_sz += p->len;
    if (ptr_queue != NULL && !ptr_queue->isPutDisabled) {
        ptr_queue->putPCAP((uint8_t *)p->payload, p->len);
    }
#ifdef LOG_TRAFFIC
    if (p->len < sizeof(struct eth_hdr)) {  // mac_s[6] + mac_d[6] + lwip_ieee_eth_type[2]
        ESP_LOGI(TAG, "new_netif_linkoutput_fn '%c%c%d' UNDEF [%d]: %s", netif->name[0], netif->name[1], netif->num, p->len,
                 bin2hex(p->payload, p->len).c_str());
    } else {
        dump(false, p, netif);
    }
#endif  // LOG_TRAFFIC
    return old_netif_linkoutput_fn(netif, p);
}

//-------------------------------------------------------------------------------------
void netif_callbacks_set() {
    url_list.clear();
    sta_list.clear();
    start_time = esp_timer_get_time();
    if (netif_default == NULL) {
        ESP_LOGI(TAG, "NO default netif");
        return;
    }
    ESP_LOGI(TAG, "default netif '%c%c%d'", netif_default->name[0], netif_default->name[1], netif_default->num);
    dhcp_client_hostname[0] = 0;
    netif_callbacks_in_sz = netif_callbacks_out_sz = 0;
    old_netif_input_fn = netif_default->input;
    netif_default->input = new_netif_input_fn;

    old_netif_linkoutput_fn = netif_default->linkoutput;
    netif_default->linkoutput = new_netif_linkoutput_fn;
}

std::string netif_callbacks_restore() {
    if (old_netif_input_fn != NULL) {
        netif_default->input = old_netif_input_fn;
    } else {
        ESP_LOGE(TAG, "netif_callbacks_restore. old_netif_input_fn == NULL");
    }
    if (old_netif_linkoutput_fn != NULL) {
        netif_default->linkoutput = old_netif_linkoutput_fn;
    } else {
        ESP_LOGE(TAG, "netif_callbacks_restore. old_netif_linkoutput_fn == NULL");
    }
    std::stringstream o;
    o << "{\"sta_list\":[";
    for (auto it = sta_list.begin(); it != sta_list.end(); it++) {
        if (it != sta_list.begin()) o << ",";
        o << it->c_str();
    }
    o << "],\n\"url_lst\":[";
    for (auto it = url_list.begin(); it != url_list.end(); it++) {
        if (it != url_list.begin()) o << ",";
        o << it->c_str();
    }
    o << "]}";
    url_list.clear();
    sta_list.clear();
    return o.str().c_str();
}

void netif_callbacks_set_queue(PcapFileQueue *queue) {
    ptr_queue = queue;
}

char *netif_callbacks_get_hostname(uint8_t *mac, char *hostname, int max_hostname_sz) {
    if (memcmp(mac, dhcp_client_mac, 6) == 0) {
        strncpy(hostname, dhcp_client_hostname, max_hostname_sz - 1);
        hostname[max_hostname_sz - 1] = 0;
    } else {
        hostname[0] = 0;
    }
    return hostname;
}
