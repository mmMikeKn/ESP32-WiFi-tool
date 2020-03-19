#include <string.h>
#include <string>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/netif.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"

#include "common.hpp"
#include "icon_maps.h"
#include "menu_semaphore.h"
#include "web_host.hpp"
#include "wifi_func.hpp"
#include "wifi_pkt.h"

#define SCR_LINE_HD 0
#define SCR_LINE_SSID 1
#define SCR_LINE_PASWD 2
#define SCR_LINE_AP_MAC 3
#define SCR_LINE_STA_MAC 4
#define SCR_LINE_AP_IP 5
#define SCR_LINE_URI 6
#define SCR_LINE_WARN 7

static const char *TAG = "WebHost";

static uint8_t sta_mac[6], ap_mac[6];
static bool is_connected_sta = false;
static int deauth_pkg_cnt = 0;
static httpd_handle_t server = NULL;

//----------------------------------------------------------------
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "set ip: " IPSTR, IP2STR(&(((ip_event_ap_staipassigned_t *)event_data)->ip)));
    scr_printf(0, 16 * SCR_LINE_AP_IP, ST7735_LightGrey, ST7735_Black, IPSTR, IP2STR(&(((ip_event_ap_staipassigned_t *)event_data)->ip)));
    return;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case WIFI_EVENT_AP_START:
            break;
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "STA " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
            memcpy(sta_mac, event->mac, 6);
            is_connected_sta = true;
            scr_printf(0, 16 * SCR_LINE_STA_MAC, ST7735_Green, ST7735_Black, MACSTR, MAC2STR(sta_mac));
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "STA " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
            is_connected_sta = false;
            scr_printf(0, 16 * SCR_LINE_STA_MAC, ST7735_DarkGrey, ST7735_Black, MACSTR, MAC2STR(sta_mac));
            break;
        }
    }
}
//----------------------------------------------------------------

static void IRAM_ATTR wifi_sniffer_cb(void *recv_buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *spkt = (wifi_promiscuous_pkt_t *)recv_buf;
    if (spkt->rx_ctrl.rx_state != 0 || type == WIFI_PKT_MISC)
        return;
    if (memcmp(ap_mac, ((wifi_common_body_t *)(spkt->payload))->address1, 6) == 0 || memcmp(ap_mac, ((wifi_common_body_t *)(spkt->payload))->address2, 6) == 0) {
        if ((((wifi_frame_ctrl_t *)(spkt->payload))->type == 0 &&
             (((wifi_frame_ctrl_t *)(spkt->payload))->subtype == 0xC || ((wifi_frame_ctrl_t *)(spkt->payload))->subtype == 0xA))) {
            deauth_pkg_cnt++;
            ESP_LOGI(TAG, "wifi deauth t:%x st:%x " MACSTR " -> " MACSTR,
                     ((wifi_frame_ctrl_t *)(spkt->payload))->type,
                     ((wifi_frame_ctrl_t *)(spkt->payload))->subtype,
                     MAC2STR(((wifi_common_body_t *)(spkt->payload))->address2),
                     MAC2STR(((wifi_common_body_t *)(spkt->payload))->address1));
        }
    }
}

//----------------------------------------------------------------

static esp_err_t load_short_file(httpd_req_t *req, const char *fname) {
    httpd_resp_set_type(req, strstr(fname, ".jsn") != NULL ? "application/json" : "text/plain");
    char buf[512];
    int n;
    std::string body = "";
    FILE *fd = fopen(fname, "r");
    if (fd) {
        while ((n = fread(buf, 1, sizeof(buf), fd)) > 0)
            body.append(buf, n);
        fclose(fd);
    }
    httpd_resp_send(req, body.c_str(), body.size());
    ESP_LOGI(TAG, "download short file [%s] body:'%s'", fname, body.c_str());
    return ESP_OK;
}

static esp_err_t load_long_file_handler(httpd_req_t *req, const char *fname, const char *fname_out) {
    FILE *fd = fopen(fname, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to read file %s", fname);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Failed to read file");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "download %s", fname);
    char buf[2048];
    int n;
    httpd_resp_set_type(req, "application/cap");
    sprintf(buf, "attachment; filename=\"%s\"", fname_out);
    httpd_resp_set_hdr(req, "Content-Disposition", buf);
    while ((n = fread(buf, 1, sizeof(buf), fd)) != 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(fd);
            ESP_LOGE(TAG, "File sending failed!");
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }
    }
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "%s file sending complete", fname);
    return ESP_OK;
}

static esp_err_t download_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "download_get_handler: [%s]", req->uri);
    scr_printf(0, 16 * SCR_LINE_URI, SCR_DUMP_COLOR, "GET %s", req->uri);

    if (strcmp(req->uri, "/favicon.ico") == 0) {
        extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
        extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
        httpd_resp_set_type(req, "image/x-icon");
        httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
        return ESP_OK;
    }
    if (strcmp(FNAME_SCAN_AP_RESULT_JSON, req->uri) == 0)
        return load_short_file(req, FNAME_SCAN_AP_RESULT_JSON);
    if (strcmp(FNAME_WIFI_SNIFFED_MAC_JSON, req->uri) == 0)
        return load_short_file(req, FNAME_WIFI_SNIFFED_MAC_JSON);
    if (strcmp(FNAME_WIFI_ROUTER_INFO_JSON, req->uri) == 0)
        return load_short_file(req, FNAME_WIFI_ROUTER_INFO_JSON);
    if (strcmp(FNAME_WIFI_FILTER_MAC_TXT, req->uri) == 0)
        return load_short_file(req, FNAME_WIFI_FILTER_MAC_TXT);
    if (strcmp(FNAME_WIFI_ROUTER_TXT, req->uri) == 0)
        return load_short_file(req, FNAME_WIFI_ROUTER_TXT);
    if (strcmp(FNAME_WIFI_BACKON_SPAM, req->uri) == 0)
        return load_short_file(req, FNAME_WIFI_BACKON_SPAM);
    if (strcmp(FNAME_SCAN_AP_RESULT_TXT, req->uri) == 0)
        return load_short_file(req, FNAME_SCAN_AP_RESULT_TXT);
    if (strcmp(FNAME_WIFI_PCUP, req->uri) == 0)
        return load_long_file_handler(req, FNAME_WIFI_PCUP, "wifi.pcap");
    if (strcmp(FNAME_IP_PCUP, req->uri) == 0)
        return load_long_file_handler(req, FNAME_IP_PCUP, "ip.pcap");

    if (strcmp(req->uri, "/") == 0 || strcmp(req->uri, "/index.html") == 0) {
        extern const unsigned char index_html_start[] asm("_binary_index_html_start");
        extern const unsigned char index_html_end[] asm("_binary_index_html_end");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "'%s' URI is not available", req->uri);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "URI is not available");
    return ESP_FAIL;
}

static esp_err_t post_handler(httpd_req_t *req) {
    char buf[512];
    if (req->content_len > (sizeof(buf) - 1)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Too long body");
        return ESP_FAIL;
    }
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    ESP_LOGI(TAG, "uri=[%s] data=[%s]", req->uri, buf);
    FILE *f = fopen(req->uri, "w");
    if (f == NULL) {
        scr_error("Failed to open file %s for writing", req->uri);
        return ESP_OK;
    }
    fputs(buf, f);
    fclose(f);
    return ESP_OK;
}

//----------------------------------------------------------------
static bool start(std::string &ssid, std::string &password) {
    is_connected_sta = false;
    deauth_pkg_cnt = 0;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ERROR_CHECK_RET_BOOL(esp_wifi_init(&cfg));

    ERROR_CHECK_RET_BOOL(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ERROR_CHECK_RET_BOOL(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ip_event_handler, NULL));

    ERROR_CHECK_RET_BOOL(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ERROR_CHECK_RET_BOOL(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    strncpy((char *)ap_config.ap.ssid, ssid.c_str(), sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.channel = 1;
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.max_connection = 4;
    ap_config.ap.beacon_interval = 100;

    if (password.length() == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strcpy((char *)ap_config.ap.password, password.c_str());
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ERROR_CHECK_RET_BOOL(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ERROR_CHECK_RET_BOOL(esp_wifi_start());

    ERROR_CHECK_RET_BOOL(esp_wifi_get_mac(WIFI_IF_AP, ap_mac));
    ESP_LOGI(TAG, "wifi_init finished. mac=" MACSTR " ssid=[%s] password=[%s]", MAC2STR(ap_mac), ssid.c_str(), password.c_str());
    scr_printf(0, 16 * SCR_LINE_AP_MAC, ST7735_Green, ST7735_Black, MACSTR, MAC2STR(ap_mac));

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous(true));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_filter(&filter));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
    ERROR_CHECK_RET_BOOL(esp_wifi_set_channel(ap_config.ap.channel, WIFI_SECOND_CHAN_NONE));

    ESP_LOGI(TAG, "--------- starting HTTP server ----------");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    ERROR_CHECK_RET_BOOL(httpd_start(&server, &config));

    httpd_uri_t file_download = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = download_get_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &file_download);

    httpd_uri_t save_file = {
        .uri = "/*",
        .method = HTTP_POST,
        .handler = post_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &save_file);
    return true;
}

static void stop() {
    ESP_LOGI(TAG, "--------- stoping HTTP server ----------");
    if (server != NULL)
        httpd_stop(server);
    server = NULL;
    esp_wifi_set_promiscuous(false);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ip_event_handler);
    esp_wifi_stop();
}

//----------------------------------------------------------------

void wifi_AP_WebHost(void *arg) {
    std::string password = "";
    std::string ssid = "ESPH";

    drawHeader(eye_icon16x16, (char *)arg);

    scr_printf(0, 16 * SCR_LINE_SSID, SCR_INFO_COLOR, "SSID:%s", ssid.c_str());
    scr_printf(0, 16 * SCR_LINE_PASWD, SCR_INFO_COLOR, "P:%s", password.c_str());

    start(ssid, password);
    while (getKeyPressed(200 / portTICK_RATE_MS) != GPIO_KEY_CANCEL) {
        if (deauth_pkg_cnt > 0) {
            scr_printf(0, 16 * SCR_LINE_WARN, ST7735_Red, ST7735_Black, "Deauth PKG:%d", deauth_pkg_cnt);
        }
    }
    stop();
    xSemaphoreGive(menu_semaphore);
    vTaskDelete(NULL);
}
