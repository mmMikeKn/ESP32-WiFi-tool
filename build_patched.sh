#!/bin/bash
umask 000
echo --- patch libnet80211.a ----
ar r /opt/esp/idf/components/esp_wifi/lib/esp32/libnet80211.a pathed/ieee80211_output.o
echo --- patch lwip ----
rm -rf /opt/esp/idf/components/lwip/lwip
ln -s /project/lwip_nat /opt/esp/idf/components/lwip/lwip
echo --- patch mac_addr.c ----
rm -f /opt/esp/idf/components/esp_common/src/mac_addr.c
ln -s /project/pathed/mac_addr.c /opt/esp/idf/components/esp_common/src/mac_addr.c
echo --- build ----
idf.py build 



