set(COMPONENT_SRCS "common.cpp" "icon_maps.cpp" "PcapFileQueue.cpp" "netif_callbacks.cpp")

set(COMPONENT_ADD_INCLUDEDIRS "include")

set(COMPONENT_REQUIRES tft_st7735)

register_component()
