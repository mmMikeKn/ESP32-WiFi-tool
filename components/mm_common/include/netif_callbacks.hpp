#ifndef _netif_callbacks_HPP
#define _netif_callbacks_HPP

#include "PcapFileQueue.hpp"

extern int netif_callbacks_in_sz, netif_callbacks_out_sz;

void netif_callbacks_set();
std::string netif_callbacks_restore();
void netif_callbacks_set_queue(PcapFileQueue *queue);
char *netif_callbacks_get_hostname(uint8_t *mac, char *hostname, int max_hostname_sz);

#endif //_netif_callbacks_HPP