#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"

extern "C" {
#include "esp_private/wifi.h"
#include "esp_wifi_default.h"
}

#include "wifi_func.hpp"

static const char *TAG = "init";
#define SD_MISO_GPIO GPIO_NUM_19
#define SD_MOSI_GPIO GPIO_NUM_23
#define SD_CLK_GPIO GPIO_NUM_18
#define SD_CS_GPIO GPIO_NUM_5
#define SD_SPI_HOST VSPI_HOST
#define SD_DMA_CHANNEL 2

bool init_SD() {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = SD_MISO_GPIO;
    slot_config.gpio_mosi = SD_MOSI_GPIO;
    slot_config.gpio_sck = SD_CLK_GPIO;
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.dma_channel = SD_DMA_CHANNEL;  // != ST7735_DMA_CHANNEL

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = SD_MOUNT_DMA_SZ};

    sdmmc_card_t *card;
    ERROR_CHECK_RET_BOOL(esp_vfs_fat_sdmmc_mount(SD_MOUNT_PATH, &host, &slot_config, &mount_config, &card));
    ESP_LOGI(TAG, "mount to '%s' name: '%s' size: %lluMB\n", SD_MOUNT_PATH, card->cid.name, ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
    return true;
}

static bool init_ip(std::string &ifkey) {
    // -------------- create 'st0' netif ---------
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        scr_error("sta_netif = NULL");
        return false;
    }
    // -------------- create 'ap0' netif ---------
    // esp_netif_create_default_wifi_ap();
    esp_netif_config_t esp_netif_cfg;
    memset(&esp_netif_cfg, 0, sizeof(esp_netif_cfg));
    esp_netif_cfg.base = ESP_NETIF_BASE_DEFAULT_WIFI_AP;
    esp_netif_cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP;
    esp_netif_cfg.driver = NULL;
    esp_netif_t *ap_netif = esp_netif_new(&esp_netif_cfg);
    if (ap_netif == NULL) {
        scr_error("esp_netif_new = NULL");
        return false;
    }
    ERROR_CHECK_RET_BOOL(esp_netif_attach_wifi_ap(ap_netif));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_default_wifi_ap_handlers());
/*    
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_ip_info_t ip_info;
    // .ip mast be = .gw (error an LWIP ARP source?)
    IP4_ADDR(&ip_info.ip, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ERROR_CHECK_RET_BOOL(esp_netif_set_ip_info(ap_netif, &ip_info));
    ERROR_CHECK_RET_BOOL(esp_netif_dhcps_start(ap_netif));
*/
    ifkey = std::string(esp_netif_get_ifkey(ap_netif));
    return true;
}

bool init_netif(std::string &ifkey) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ERROR_CHECK_RET_BOOL(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ERROR_CHECK_RET_BOOL(ret);
    ERROR_CHECK_RET_BOOL(esp_netif_init());
    ERROR_CHECK_RET_BOOL(esp_event_loop_create_default());

    ESP_LOGI(TAG, "init_netif finish");
    return init_ip(ifkey);
}
