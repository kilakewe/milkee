#include "server_bsp.h"
#include "button_bsp.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdcard_bsp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "server_bsp";

static const char *kNvsNamespace = "BrowserUpload";
static const char *kNvsKeyRotation = "rotation";
static const char *kNvsKeyImageRotation = "image_rotation";

static uint16_t s_rotation_deg = 180;
static uint16_t s_image_rotation_deg = 180;
static uint64_t s_last_activity_us = 0;
static portMUX_TYPE s_activity_mux = portMUX_INITIALIZER_UNLOCKED;

static void server_bsp_mark_activity_internal(void)
{
    const uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_activity_mux);
    s_last_activity_us = now;
    portEXIT_CRITICAL(&s_activity_mux);
}

uint64_t server_bsp_get_last_activity_us(void)
{
    portENTER_CRITICAL(&s_activity_mux);
    const uint64_t v = s_last_activity_us;
    portEXIT_CRITICAL(&s_activity_mux);
    return v;
}

uint16_t server_bsp_get_rotation(void)
{
    return s_rotation_deg;
}

uint16_t server_bsp_get_image_rotation(void)
{
    return s_image_rotation_deg;
}

esp_err_t server_bsp_set_rotation(uint16_t rotation_deg)
{
    if (!(rotation_deg == 0 || rotation_deg == 90 || rotation_deg == 180 || rotation_deg == 270))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_rotation_deg = rotation_deg;

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_u16(nvs, kNvsKeyRotation, s_rotation_deg);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void server_bsp_load_rotation_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS open failed (%s); using default rotation %u", esp_err_to_name(err), (unsigned)s_rotation_deg);
        s_image_rotation_deg = s_rotation_deg;
        return;
    }

    uint16_t rot = 0;
    uint16_t img_rot = 0;

    const esp_err_t err_rot = nvs_get_u16(nvs, kNvsKeyRotation, &rot);
    const esp_err_t err_img = nvs_get_u16(nvs, kNvsKeyImageRotation, &img_rot);

    nvs_close(nvs);

    if (err_rot == ESP_OK && (rot == 0 || rot == 90 || rot == 180 || rot == 270))
    {
        s_rotation_deg = rot;
        ESP_LOGI(TAG, "Loaded rotation from NVS: %u", (unsigned)s_rotation_deg);
    }
    else
    {
        ESP_LOGI(TAG, "No saved rotation in NVS; using default rotation %u", (unsigned)s_rotation_deg);
    }

    if (err_img == ESP_OK && (img_rot == 0 || img_rot == 90 || img_rot == 180 || img_rot == 270))
    {
        s_image_rotation_deg = img_rot;
        ESP_LOGI(TAG, "Loaded image rotation from NVS: %u", (unsigned)s_image_rotation_deg);
    }
    else
    {
        // If no image rotation was saved yet, assume it matches the current rotation.
        s_image_rotation_deg = s_rotation_deg;
    }
}

// Static web UI is served from the SD card under:
//   /sdcard/system/
// Repository copy lives under:
//   sd-content/system/
static const char *kSdWebRoot = "/sdcard/system";

static const char *kUserCurrentImgDir = "/sdcard/user/current-img";
static const char *kUserCurrentBmpPath = "/sdcard/user/current-img/user_send.bmp";

#define MIN(x, y) ((x < y) ? (x) : (y))
#define READ_LEN_MAX (10 * 1024) // Buffer area for receiving data
#define SEND_LEN_MAX (5 * 1024)  // Data for sending response

#define EXAMPLE_ESP_WIFI_SSID "esp_network"
#define EXAMPLE_ESP_WIFI_PASS "1234567890"
#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4

EventGroupHandle_t server_groups;

/*Callback function*/
esp_err_t get_static_callback(httpd_req_t *req);
esp_err_t post_dataup_callback(httpd_req_t *req);

// Rotation settings API
esp_err_t get_rotation_callback(httpd_req_t *req);
esp_err_t post_rotation_callback(httpd_req_t *req);

/*html 代码*/
static void server_bsp_ensure_dir(const char *path)
{
    struct stat st = {};
    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            return;
        }
        ESP_LOGW(TAG, "Path exists but is not a directory: %s", path);
        return;
    }

    if (mkdir(path, 0775) != 0)
    {
        if (errno != EEXIST)
        {
            ESP_LOGW(TAG, "Failed to create dir %s (errno=%d)", path, errno);
        }
    }
}

void http_server_init(void) {
    server_groups         = xEventGroupCreate();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // We register more than the HTTPD_DEFAULT_CONFIG() handler limit.
    // If this stays too low, later registrations (like /dataUP) will fail and uploads will 404.
    config.max_uri_handlers = 16;
    config.uri_match_fn     = httpd_uri_match_wildcard; /*Wildcard enabling*/
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    server_bsp_mark_activity_internal();
    server_bsp_load_rotation_from_nvs();

    // Ensure the user image save directory exists.
    server_bsp_ensure_dir("/sdcard/user");
    server_bsp_ensure_dir(kUserCurrentImgDir);

    // Rotation settings API
    httpd_uri_t uri_rot = {};
    uri_rot.uri         = "/api/rotation";
    uri_rot.user_ctx    = NULL;
    uri_rot.method      = HTTP_GET;
    uri_rot.handler     = get_rotation_callback;
    httpd_register_uri_handler(server, &uri_rot);

    uri_rot.method  = HTTP_POST;
    uri_rot.handler = post_rotation_callback;
    httpd_register_uri_handler(server, &uri_rot);

    httpd_uri_t uri_post = {};
    uri_post.uri         = "/dataUP";
    uri_post.method      = HTTP_POST;
    uri_post.handler     = post_dataup_callback;
    uri_post.user_ctx    = NULL;
    httpd_register_uri_handler(server, &uri_post);

    // Static file server from SD card (/sdcard/system).
    httpd_uri_t uri_static = {};
    uri_static.uri         = "/*";
    uri_static.method      = HTTP_GET;
    uri_static.handler     = get_static_callback;
    uri_static.user_ctx    = NULL;
    httpd_register_uri_handler(server, &uri_static);
}

static const char *server_bsp_content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    }
    if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(ext, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(ext, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

static esp_err_t server_bsp_send_sd_file(httpd_req_t *req, const char *sd_path)
{
    char *resp_str = (char *)heap_caps_malloc(SEND_LEN_MAX + 1, MALLOC_CAP_SPIRAM);
    if (!resp_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    size_t off = 0;
    size_t len = sdcard_read_offset(sd_path, resp_str, SEND_LEN_MAX, off);
    if (len == 0) {
        heap_caps_free(resp_str);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found on SD");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Serving SD file: %s", sd_path);

    while (len) {
        httpd_resp_send_chunk(req, resp_str, len);
        off += len;
        len = sdcard_read_offset(sd_path, resp_str, SEND_LEN_MAX, off);
    }

    httpd_resp_send_chunk(req, NULL, 0);
    heap_caps_free(resp_str);
    return ESP_OK;
}

static bool server_bsp_uri_is_safe(const char *uri)
{
    // Very small allowlist: block path traversal and backslashes.
    if (strstr(uri, "..") != NULL) {
        return false;
    }
    if (strchr(uri, '\\') != NULL) {
        return false;
    }
    return true;
}

static void server_bsp_normalize_uri_path(const char *uri, char *out_path, size_t out_path_len)
{
    // Strip query string ("?foo=bar") if present.
    const char *q = strchr(uri, '?');
    const size_t uri_len = q ? (size_t)(q - uri) : strlen(uri);

    // Root -> /index.html
    if (uri_len == 1 && uri[0] == '/') {
        snprintf(out_path, out_path_len, "%s/index.html", kSdWebRoot);
        return;
    }

    // Copy the path part into a small buffer so we can check trailing '/'.
    char tmp[128] = {0};
    snprintf(tmp, sizeof(tmp), "%.*s", (int)uri_len, uri);

    if (tmp[0] == '\0') {
        snprintf(out_path, out_path_len, "%s/index.html", kSdWebRoot);
        return;
    }

    // Directory -> append index.html
    const size_t tlen = strlen(tmp);
    if (tlen > 0 && tmp[tlen - 1] == '/') {
        snprintf(out_path, out_path_len, "%s%.*sindex.html", kSdWebRoot, (int)tlen, tmp);
        return;
    }

    snprintf(out_path, out_path_len, "%s%.*s", kSdWebRoot, (int)uri_len, uri);
}

/*The callback function for handling GET requests*/
esp_err_t get_static_callback(httpd_req_t *req)
{
    const char *uri = req->uri;
    server_bsp_mark_activity_internal();

    if (!server_bsp_uri_is_safe(uri)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    char sd_path[192] = {0};
    server_bsp_normalize_uri_path(uri, sd_path, sizeof(sd_path));

    httpd_resp_set_type(req, server_bsp_content_type_for_path(sd_path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return server_bsp_send_sd_file(req, sd_path);
}

esp_err_t get_rotation_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[64] = {0};
    snprintf(resp, sizeof(resp), "{\"rotation\":%u}\n", (unsigned)server_bsp_get_rotation());
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_rotation_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    char body[32] = {0};
    size_t remaining = req->content_len;
    size_t off = 0;

    while (remaining > 0)
    {
        const size_t can_read = sizeof(body) - 1 - off;
        if (can_read == 0)
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
            return ESP_OK;
        }

        int ret = httpd_req_recv(req, body + off, MIN(remaining, can_read));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_OK;
        }

        off += (size_t)ret;
        remaining -= (size_t)ret;
    }

    char *end = NULL;
    long deg = strtol(body, &end, 10);
    if (end == body)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid rotation");
        return ESP_OK;
    }

    esp_err_t err = server_bsp_set_rotation((uint16_t)deg);
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unsupported rotation (use 0, 90, 180, or 270)");
        return ESP_OK;
    }

    // Re-display the last uploaded image (if any) using the new rotation.
    xEventGroupSetBits(server_groups, set_bit_button(2));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[64] = {0};
    snprintf(resp, sizeof(resp), "{\"rotation\":%u}\n", (unsigned)server_bsp_get_rotation());
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_dataup_callback(httpd_req_t *req) {
    server_bsp_mark_activity_internal();
    char       *buf        = (char *) heap_caps_malloc(READ_LEN_MAX + 1, MALLOC_CAP_SPIRAM);
    size_t      sdcard_len = 0;
    size_t      remaining  = req->content_len;
    const char *uri        = req->uri;
    size_t      ret;
    ESP_LOGI("TAG", "用户POST的URI是:%s,字节:%d", uri, remaining);
    xEventGroupSetBits(server_groups, set_bit_button(0)); 
    sdcard_write_offset(kUserCurrentBmpPath, NULL, 0, 0);
    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, READ_LEN_MAX))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }
        size_t req_len = sdcard_write_offset(kUserCurrentBmpPath, buf, ret, 1);
        sdcard_len += req_len; // Final comparison result
        remaining -= ret;      // Subtract the data that has already been received
        server_bsp_mark_activity_internal();
    }
    xEventGroupSetBits(server_groups, set_bit_button(1)); 
    if (sdcard_len == req->content_len) {
        // Record the rotation that was active when this image was uploaded.
        s_image_rotation_deg = server_bsp_get_rotation();
        {
            nvs_handle_t nvs = 0;
            esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
            if (err == ESP_OK)
            {
                err = nvs_set_u16(nvs, kNvsKeyImageRotation, s_image_rotation_deg);
                if (err == ESP_OK) {
                    nvs_commit(nvs);
                }
                nvs_close(nvs);
            }
        }

        httpd_resp_send_chunk(req, "上传成功", strlen("上传成功"));
        xEventGroupSetBits(server_groups, set_bit_button(2));
    } 
    else {
        httpd_resp_send_chunk(req, "上传失败", strlen("上传失败"));
        xEventGroupSetBits(server_groups, set_bit_button(3));
    } 
    httpd_resp_send_chunk(req, NULL, 0);

    heap_caps_free(buf);
    buf = NULL;
    return ESP_OK;
}

/*wifi ap init*/
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        xEventGroupSetBits(server_groups, set_bit_button(4));
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        xEventGroupSetBits(server_groups, set_bit_button(5));
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
    }
}

void Network_wifi_ap_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    assert(esp_netif_create_default_wifi_ap());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_AP_STAIPASSIGNED,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {};
    snprintf((char *) wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", EXAMPLE_ESP_WIFI_SSID);
    snprintf((char *) wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", EXAMPLE_ESP_WIFI_PASS);
    wifi_config.ap.channel        = EXAMPLE_ESP_WIFI_CHANNEL;
    wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
    wifi_config.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("network", "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

void set_espWifi_sleep(void) {
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(500));
}