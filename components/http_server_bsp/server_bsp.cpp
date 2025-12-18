#include "server_bsp.h"
#include "button_bsp.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdcard_bsp.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <algorithm>
#include <string>
#include <vector>

static const char *TAG = "server_bsp";

static const char *kNvsNamespace = "BrowserUpload";
static const char *kNvsKeyRotation = "rotation";
static const char *kNvsKeyImageRotation = "image_rotation";
static const char *kNvsKeyCurrentImage = "current_image"; // legacy
static const char *kNvsKeyCurrentPhotoId = "current_photo_id";
static const char *kNvsKeyPhotoSeq = "photo_seq";
static const char *kNvsKeySlideshowEnabled = "slideshow_enabled";
static const char *kNvsKeySlideshowIntervalS = "slideshow_interval_s";

// SD card paths
// Serve the Vue app build output (sd-content/web-app -> /sdcard/web-app)
static const char *kSdWebRoot = "/sdcard/web-app";
static const char *kUserPhotoDir = "/sdcard/user/current-img";
static const char *kLibraryPath = "/sdcard/user/current-img/library.json";

static const char *kFallbackDir = "/sdcard/fallback-frame";
static const char *kFallbackLandscape = "/sdcard/fallback-frame/fallback_landscape.bmp";
static const char *kFallbackPortrait = "/sdcard/fallback-frame/fallback_portrait.bmp";

static uint16_t s_rotation_deg = 180;
static uint16_t s_image_rotation_deg = 180;
static char s_current_image_path[192] = {0};
static char s_current_photo_id[64] = {0};

// When a new photo is uploaded in two steps (landscape + portrait), we may want to
// wait until the preferred variant for the *current* frame orientation is present
// before switching the display. This stores the in-progress new photo id.
static char s_pending_new_photo_id[64] = {0};

static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;

static bool s_slideshow_enabled = false;
static uint32_t s_slideshow_interval_s = 3600;

struct LibraryPhoto
{
    std::string id;
    std::string landscape;
    std::string portrait;
};

static std::vector<LibraryPhoto> s_library_photos;
static std::vector<std::string> s_library_order;
static bool s_state_initialized = false;
static SemaphoreHandle_t s_library_mutex = NULL;

static uint64_t s_last_activity_us = 0;
static portMUX_TYPE s_activity_mux = portMUX_INITIALIZER_UNLOCKED;

// Forward declarations for static helpers (some are referenced before their definitions below).
static void server_bsp_update_current_image_for_rotation(void);
static void server_bsp_set_current_image_internal(const char *full_path, uint16_t img_rot);
static esp_err_t server_bsp_recv_small_body(httpd_req_t *req, char *body, size_t body_size);

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

void server_bsp_mark_activity(void)
{
    server_bsp_mark_activity_internal();
}

uint16_t server_bsp_get_rotation(void)
{
    return s_rotation_deg;
}

static bool server_bsp_slideshow_interval_is_allowed(uint32_t interval_s)
{
    switch (interval_s)
    {
    case 300:
    case 600:
    case 900:
    case 1800:
    case 3600:
    case 10800:
    case 21600:
    case 86400:
    case 259200:
    case 604800:
        return true;
    default:
        return false;
    }
}

bool server_bsp_get_slideshow_enabled(void)
{
    return s_slideshow_enabled;
}

uint32_t server_bsp_get_slideshow_interval_s(void)
{
    return s_slideshow_interval_s;
}

esp_err_t server_bsp_set_slideshow(bool enabled, uint32_t interval_s)
{
    if (!server_bsp_slideshow_interval_is_allowed(interval_s))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_slideshow_enabled = enabled;
    s_slideshow_interval_s = interval_s;

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_u8(nvs, kNvsKeySlideshowEnabled, enabled ? 1 : 0);
    if (err == ESP_OK)
    {
        err = nvs_set_u32(nvs, kNvsKeySlideshowIntervalS, s_slideshow_interval_s);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

uint16_t server_bsp_get_image_rotation(void)
{
    portENTER_CRITICAL(&s_state_mux);
    const uint16_t v = s_image_rotation_deg;
    portEXIT_CRITICAL(&s_state_mux);
    return v;
}

const char *server_bsp_get_current_image_path(void)
{
    return s_current_image_path;
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

    // Pick the correct variant for the new orientation.
    server_bsp_update_current_image_for_rotation();

    return err;
}

static bool server_bsp_ends_with_ignore_case(const char *s, const char *suffix)
{
    const size_t sl = strlen(s);
    const size_t su = strlen(suffix);
    if (sl < su)
        return false;

    const char *p = s + (sl - su);
    for (size_t i = 0; i < su; i++)
    {
        char a = p[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static uint16_t server_bsp_parse_rotation_from_filename(const char *name, uint16_t default_rot)
{
    if (!name)
        return default_rot;

    const char *p = strstr(name, "_r");
    if (!p)
        return default_rot;

    p += 2;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p)
        return default_rot;

    const uint16_t rot = (uint16_t)v;
    if (rot == 0 || rot == 90 || rot == 180 || rot == 270)
    {
        return rot;
    }
    return default_rot;
}

static bool server_bsp_photo_name_is_safe(const char *name);

static const char *server_bsp_basename(const char *path)
{
    if (!path)
    {
        return "";
    }
    const char *base = strrchr(path, '/');
    return base ? (base + 1) : path;
}

static bool server_bsp_photo_id_is_safe(const char *id)
{
    if (!id || id[0] == '\0')
    {
        return false;
    }

    // Keep IDs bounded (stored in fixed-size buffers).
    if (strlen(id) >= sizeof(s_current_photo_id))
    {
        return false;
    }

    if (strstr(id, "..") != NULL)
    {
        return false;
    }
    if (strchr(id, '/') != NULL || strchr(id, '\\') != NULL)
    {
        return false;
    }

    for (const char *p = id; *p; p++)
    {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok)
        {
            return false;
        }
    }

    return true;
}

static LibraryPhoto *server_bsp_find_photo_locked(const char *id)
{
    if (!id)
    {
        return nullptr;
    }

    for (auto &p : s_library_photos)
    {
        if (p.id == id)
        {
            return &p;
        }
    }
    return nullptr;
}

static LibraryPhoto *server_bsp_get_or_create_photo_locked(const char *id)
{
    if (!id)
    {
        return nullptr;
    }

    LibraryPhoto *p = server_bsp_find_photo_locked(id);
    if (p)
    {
        return p;
    }

    s_library_photos.push_back(LibraryPhoto{std::string(id), std::string(), std::string()});
    return &s_library_photos.back();
}

static void server_bsp_sort_order_locked(void)
{
    std::sort(s_library_order.begin(), s_library_order.end());
}

static bool server_bsp_file_exists(const char *path)
{
    struct stat st = {};
    return (path && stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static bool server_bsp_extract_photo_id_from_filename(const char *name, char *out_id, size_t out_id_len)
{
    if (!name || !out_id || out_id_len == 0)
    {
        return false;
    }

    // Expect patterns like:
    // - img_000123_r180.bmp (legacy)
    // - img_000123_L_r0.bmp
    // - img_000123_P_r90.bmp
    const char *end = strstr(name, "_r");
    if (!end)
    {
        // Also accept IDs without _r (fallback images etc) by stripping .bmp.
        const char *dot = strrchr(name, '.');
        if (!dot)
        {
            return false;
        }
        end = dot;
    }

    // Handle variant marker ("_L"/"_P") before "_r".
    const char *var = strstr(name, "_L_");
    const char *var2 = strstr(name, "_P_");
    const char *cut = end;
    if (var && var < cut)
        cut = var;
    if (var2 && var2 < cut)
        cut = var2;

    const size_t n = (size_t)(cut - name);
    if (n == 0 || n >= out_id_len)
    {
        return false;
    }

    snprintf(out_id, out_id_len, "%.*s", (int)n, name);
    return server_bsp_photo_id_is_safe(out_id);
}

static const char *server_bsp_choose_fallback_path(uint16_t rotation_deg)
{
    const bool portrait = (rotation_deg == 90 || rotation_deg == 270);
    const char *preferred = portrait ? kFallbackPortrait : kFallbackLandscape;
    if (server_bsp_file_exists(preferred))
    {
        return preferred;
    }

    // If preferred file doesn't exist, fall back to any BMP in the fallback dir.
    DIR *dir = opendir(kFallbackDir);
    if (!dir)
    {
        return "";
    }

    static char chosen[192] = {0};
    chosen[0] = '\0';

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL)
    {
        const char *n = ent->d_name;
        if (!n || n[0] == '.')
        {
            continue;
        }
        if (!server_bsp_ends_with_ignore_case(n, ".bmp"))
        {
            continue;
        }

        // Build the path safely without triggering -Wformat-truncation.
        chosen[0] = '\0';
        if (strlcpy(chosen, kFallbackDir, sizeof(chosen)) >= sizeof(chosen))
        {
            continue;
        }
        if (strlcat(chosen, "/", sizeof(chosen)) >= sizeof(chosen))
        {
            continue;
        }
        if (strlcat(chosen, n, sizeof(chosen)) >= sizeof(chosen))
        {
            continue;
        }
        break;
    }

    closedir(dir);
    return chosen;
}

static void server_bsp_clear_library_locked(void)
{
    s_library_photos.clear();
    s_library_order.clear();
}

static bool server_bsp_library_has_id_locked(const std::string &id)
{
    for (const auto &p : s_library_photos)
    {
        if (p.id == id)
        {
            return true;
        }
    }
    return false;
}

static void server_bsp_library_filter_order_locked(void)
{
    std::vector<std::string> filtered;
    filtered.reserve(s_library_order.size());

    for (const auto &id : s_library_order)
    {
        if (server_bsp_library_has_id_locked(id))
        {
            filtered.push_back(id);
        }
    }

    s_library_order.swap(filtered);
}

static void server_bsp_library_ensure_order_contains_all_photos_locked(void)
{
    for (const auto &p : s_library_photos)
    {
        bool found = false;
        for (const auto &id : s_library_order)
        {
            if (id == p.id)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            s_library_order.push_back(p.id);
        }
    }
}

static void server_bsp_merge_with_sd_locked(void)
{
    DIR *dir = opendir(kUserPhotoDir);
    if (!dir)
    {
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL)
    {
        const char *name = ent->d_name;
        if (!name || name[0] == '.')
        {
            continue;
        }
        if (!server_bsp_photo_name_is_safe(name))
        {
            continue;
        }

        char id[64] = {0};
        if (!server_bsp_extract_photo_id_from_filename(name, id, sizeof(id)))
        {
            continue;
        }

        LibraryPhoto *p = server_bsp_get_or_create_photo_locked(id);
        if (!p)
        {
            continue;
        }

        const bool has_P = (strstr(name, "_P_") != NULL);
        const bool has_L = (strstr(name, "_L_") != NULL);

        bool is_portrait = false;
        if (has_P)
        {
            is_portrait = true;
        }
        else if (has_L)
        {
            is_portrait = false;
        }
        else
        {
            const uint16_t rot = server_bsp_parse_rotation_from_filename(name, 0);
            is_portrait = (rot == 90 || rot == 270);
        }

        if (is_portrait)
        {
            if (p->portrait.empty())
            {
                p->portrait = name;
            }
        }
        else
        {
            if (p->landscape.empty())
            {
                p->landscape = name;
            }
        }

        if (!server_bsp_library_has_id_locked(p->id))
        {
            s_library_order.push_back(p->id);
        }
    }

    closedir(dir);
    server_bsp_library_filter_order_locked();
    server_bsp_library_ensure_order_contains_all_photos_locked();
}

static bool server_bsp_load_library_from_sd_locked(void)
{
    FILE *fp = fopen(kLibraryPath, "r");
    if (!fp)
    {
        // If no library.json, we'll merge from SD later.
        server_bsp_clear_library_locked();
        server_bsp_merge_with_sd_locked();
        return !s_library_photos.empty();
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > (64 * 1024))
    {
        fclose(fp);
        server_bsp_clear_library_locked();
        server_bsp_merge_with_sd_locked();
        return !s_library_photos.empty();
    }

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf)
    {
        fclose(fp);
        server_bsp_clear_library_locked();
        server_bsp_merge_with_sd_locked();
        return !s_library_photos.empty();
    }

    const size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
    {
        server_bsp_clear_library_locked();
        server_bsp_merge_with_sd_locked();
        return !s_library_photos.empty();
    }

    server_bsp_clear_library_locked();

    const cJSON *photos = cJSON_GetObjectItem(root, "photos");
    if (photos && cJSON_IsArray(photos))
    {
        const int n = cJSON_GetArraySize(photos);
        for (int i = 0; i < n; i++)
        {
            const cJSON *item = cJSON_GetArrayItem(photos, i);
            if (!item || !cJSON_IsObject(item))
            {
                continue;
            }

            const cJSON *jid = cJSON_GetObjectItem(item, "id");
            if (!jid || !cJSON_IsString(jid) || !jid->valuestring)
            {
                continue;
            }

            const char *id = jid->valuestring;
            if (!server_bsp_photo_id_is_safe(id))
            {
                continue;
            }

            LibraryPhoto *p = server_bsp_get_or_create_photo_locked(id);
            if (!p)
            {
                continue;
            }

            const cJSON *jland = cJSON_GetObjectItem(item, "landscape");
            const cJSON *jport = cJSON_GetObjectItem(item, "portrait");

            if (jland && cJSON_IsString(jland) && jland->valuestring && server_bsp_photo_name_is_safe(jland->valuestring))
            {
                p->landscape = jland->valuestring;
            }
            if (jport && cJSON_IsString(jport) && jport->valuestring && server_bsp_photo_name_is_safe(jport->valuestring))
            {
                p->portrait = jport->valuestring;
            }
        }
    }

    const cJSON *order = cJSON_GetObjectItem(root, "order");
    if (order && cJSON_IsArray(order))
    {
        const int n = cJSON_GetArraySize(order);
        for (int i = 0; i < n; i++)
        {
            const cJSON *jid = cJSON_GetArrayItem(order, i);
            if (!jid || !cJSON_IsString(jid) || !jid->valuestring)
            {
                continue;
            }

            const char *id = jid->valuestring;
            if (!server_bsp_photo_id_is_safe(id))
            {
                continue;
            }

            // Only include IDs we actually have photo records for.
            if (server_bsp_find_photo_locked(id) != nullptr)
            {
                s_library_order.push_back(id);
            }
        }
    }

    cJSON_Delete(root);

    server_bsp_library_filter_order_locked();
    server_bsp_library_ensure_order_contains_all_photos_locked();
    return !s_library_photos.empty();
}

static bool server_bsp_write_library_to_sd_locked(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        return false;
    }

    cJSON_AddNumberToObject(root, "version", 1);

    cJSON *order = cJSON_AddArrayToObject(root, "order");
    if (order)
    {
        for (const auto &id : s_library_order)
        {
            cJSON_AddItemToArray(order, cJSON_CreateString(id.c_str()));
        }
    }

    cJSON *photos = cJSON_AddArrayToObject(root, "photos");
    if (photos)
    {
        for (const auto &p : s_library_photos)
        {
            cJSON *item = cJSON_CreateObject();
            if (!item)
            {
                continue;
            }

            cJSON_AddStringToObject(item, "id", p.id.c_str());
            cJSON_AddStringToObject(item, "landscape", p.landscape.c_str());
            cJSON_AddStringToObject(item, "portrait", p.portrait.c_str());
            cJSON_AddItemToArray(photos, item);
        }
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!text)
    {
        return false;
    }

    FILE *fp = fopen(kLibraryPath, "w");
    if (!fp)
    {
        cJSON_free(text);
        return false;
    }

    const size_t len = strlen(text);
    const size_t wr = fwrite(text, 1, len, fp);
    fclose(fp);
    cJSON_free(text);

    return (wr == len);
}

static void server_bsp_build_library_from_sd_scan_locked(void)
{
    server_bsp_clear_library_locked();

    DIR *dir = opendir(kUserPhotoDir);
    if (!dir)
    {
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL)
    {
        const char *name = ent->d_name;
        if (!name || name[0] == '.')
        {
            continue;
        }
        if (!server_bsp_photo_name_is_safe(name))
        {
            continue;
        }

        char id[64] = {0};
        if (!server_bsp_extract_photo_id_from_filename(name, id, sizeof(id)))
        {
            continue;
        }

        LibraryPhoto *p = server_bsp_get_or_create_photo_locked(id);
        if (!p)
        {
            continue;
        }

        const bool has_P = (strstr(name, "_P_") != NULL);
        const bool has_L = (strstr(name, "_L_") != NULL);

        bool is_portrait = false;
        if (has_P)
        {
            is_portrait = true;
        }
        else if (has_L)
        {
            is_portrait = false;
        }
        else
        {
            const uint16_t rot = server_bsp_parse_rotation_from_filename(name, 0);
            is_portrait = (rot == 90 || rot == 270);
        }

        if (is_portrait)
        {
            if (p->portrait.empty())
            {
                p->portrait = name;
            }
        }
        else
        {
            if (p->landscape.empty())
            {
                p->landscape = name;
            }
        }

        // Order is derived from photo IDs (lexicographic) when no library.json exists.
    }

    closedir(dir);

    for (const auto &p : s_library_photos)
    {
        s_library_order.push_back(p.id);
    }

    server_bsp_sort_order_locked();
}

static void server_bsp_update_current_image_for_rotation(void)
{
    if (!s_library_mutex)
    {
        return;
    }

    char cur_id[64] = {0};
    {
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
    }

    const uint16_t rot = s_rotation_deg;
    const bool want_portrait = (rot == 90 || rot == 270);

    std::string chosen_name;

    if (xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        LibraryPhoto *p = server_bsp_find_photo_locked(cur_id);
        if (p)
        {
            const std::string &preferred = want_portrait ? p->portrait : p->landscape;
            const std::string &secondary = want_portrait ? p->landscape : p->portrait;

            if (!preferred.empty())
            {
                chosen_name = preferred;
            }
            else if (!secondary.empty())
            {
                chosen_name = secondary;
            }
        }

        xSemaphoreGive(s_library_mutex);
    }

    if (chosen_name.empty())
    {
        const char *fb = server_bsp_choose_fallback_path(rot);
        if (fb && fb[0] != '\0')
        {
            server_bsp_set_current_image_internal(fb, rot);
        }
        else
        {
            server_bsp_set_current_image_internal("", rot);
        }
        return;
    }

    char full[192] = {0};
    snprintf(full, sizeof(full), "%s/%s", kUserPhotoDir, chosen_name.c_str());

    if (!server_bsp_file_exists(full))
    {
        const char *fb = server_bsp_choose_fallback_path(rot);
        if (fb && fb[0] != '\0')
        {
            server_bsp_set_current_image_internal(fb, rot);
        }
        else
        {
            server_bsp_set_current_image_internal("", rot);
        }
        return;
    }

    const uint16_t default_img_rot = want_portrait ? 90 : 0;
    const uint16_t img_rot = server_bsp_parse_rotation_from_filename(chosen_name.c_str(), default_img_rot);
    server_bsp_set_current_image_internal(full, img_rot);
}

static void server_bsp_save_current_photo_id_to_nvs(const char *id)
{
    nvs_handle_t nvs = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) != ESP_OK)
    {
        return;
    }

    nvs_set_str(nvs, kNvsKeyCurrentPhotoId, id ? id : "");
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void server_bsp_set_current_photo_id_internal(const char *id)
{
    const char *safe = (id && id[0] != '\0') ? id : "";

    portENTER_CRITICAL(&s_state_mux);
    snprintf(s_current_photo_id, sizeof(s_current_photo_id), "%.*s", (int)sizeof(s_current_photo_id) - 1, safe);
    // Switching the current photo resolves any pending "new upload" state.
    s_pending_new_photo_id[0] = '\0';
    portEXIT_CRITICAL(&s_state_mux);

    server_bsp_save_current_photo_id_to_nvs(safe);
    server_bsp_update_current_image_for_rotation();
}

static void server_bsp_refresh_library_from_sd_locked(void)
{
    if (!server_bsp_load_library_from_sd_locked())
    {
        server_bsp_build_library_from_sd_scan_locked();
        (void)server_bsp_write_library_to_sd_locked();
    }
}

static void server_bsp_ensure_library_loaded(void)
{
    if (!s_library_mutex)
    {
        return;
    }

    if (xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
        return;
    }

    if (s_library_photos.empty() && s_library_order.empty())
    {
        server_bsp_refresh_library_from_sd_locked();
    }

    xSemaphoreGive(s_library_mutex);
}

static void server_bsp_set_current_photo_id_from_legacy_path_if_needed(void)
{
    char id[64] = {0};

    // If we already have a photo ID saved, nothing to do.
    {
        portENTER_CRITICAL(&s_state_mux);
        if (s_current_photo_id[0] != '\0')
        {
            portEXIT_CRITICAL(&s_state_mux);
            return;
        }
        portEXIT_CRITICAL(&s_state_mux);
    }

    const char *legacy = server_bsp_get_current_image_path();
    const char *base = server_bsp_basename(legacy);
    if (!base || base[0] == '\0')
    {
        return;
    }

    if (!server_bsp_extract_photo_id_from_filename(base, id, sizeof(id)))
    {
        return;
    }

    server_bsp_set_current_photo_id_internal(id);
}

static void server_bsp_set_current_image_internal(const char *full_path, uint16_t img_rot)
{
    portENTER_CRITICAL(&s_state_mux);
    snprintf(s_current_image_path, sizeof(s_current_image_path), "%s", full_path ? full_path : "");
    s_image_rotation_deg = img_rot;
    portEXIT_CRITICAL(&s_state_mux);

    nvs_handle_t nvs = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) == ESP_OK)
    {
        nvs_set_str(nvs, kNvsKeyCurrentImage, s_current_image_path);
        nvs_set_u16(nvs, kNvsKeyImageRotation, s_image_rotation_deg);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void server_bsp_load_state_from_nvs(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "NVS open failed (%s); using defaults", esp_err_to_name(err));

        s_slideshow_enabled = false;
        s_slideshow_interval_s = 3600;

        portENTER_CRITICAL(&s_state_mux);
        s_current_photo_id[0] = '\0';
        s_image_rotation_deg = s_rotation_deg;
        s_current_image_path[0] = '\0';
        portEXIT_CRITICAL(&s_state_mux);
        return;
    }

    uint16_t rot = 0;
    uint16_t img_rot = 0;
    uint8_t slideshow_en_u8 = 0;
    uint32_t slideshow_interval_s = 0;

    const esp_err_t err_rot = nvs_get_u16(nvs, kNvsKeyRotation, &rot);
    const esp_err_t err_img = nvs_get_u16(nvs, kNvsKeyImageRotation, &img_rot);

    size_t cur_len = sizeof(s_current_image_path);
    const esp_err_t err_cur = nvs_get_str(nvs, kNvsKeyCurrentImage, s_current_image_path, &cur_len);

    size_t id_len = sizeof(s_current_photo_id);
    const esp_err_t err_id = nvs_get_str(nvs, kNvsKeyCurrentPhotoId, s_current_photo_id, &id_len);

    const esp_err_t err_sl_en = nvs_get_u8(nvs, kNvsKeySlideshowEnabled, &slideshow_en_u8);
    const esp_err_t err_sl_int = nvs_get_u32(nvs, kNvsKeySlideshowIntervalS, &slideshow_interval_s);

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
    }
    else
    {
        // If no image rotation was saved yet, assume it matches the current rotation.
        s_image_rotation_deg = s_rotation_deg;
    }

    if (err_cur == ESP_OK && s_current_image_path[0] != '\0')
    {
        ESP_LOGI(TAG, "Loaded legacy current image from NVS: %s", s_current_image_path);
    }
    else
    {
        s_current_image_path[0] = '\0';
    }

    if (err_id == ESP_OK && server_bsp_photo_id_is_safe(s_current_photo_id))
    {
        ESP_LOGI(TAG, "Loaded current photo id from NVS: %s", s_current_photo_id);
    }
    else
    {
        s_current_photo_id[0] = '\0';
    }

    if (err_sl_en == ESP_OK)
    {
        s_slideshow_enabled = (slideshow_en_u8 != 0);
    }
    if (err_sl_int == ESP_OK && server_bsp_slideshow_interval_is_allowed(slideshow_interval_s))
    {
        s_slideshow_interval_s = slideshow_interval_s;
    }
}

esp_err_t server_bsp_select_next_photo(void)
{
    server_bsp_ensure_library_loaded();

    std::string next_id;

    if (!s_library_mutex)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    if (s_library_order.empty())
    {
        xSemaphoreGive(s_library_mutex);
        server_bsp_set_current_photo_id_internal("");
        return ESP_ERR_NOT_FOUND;
    }

    char cur_id[64] = {0};
    {
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
    }

    size_t idx = 0;
    bool found = false;
    for (size_t i = 0; i < s_library_order.size(); i++)
    {
        if (s_library_order[i] == cur_id)
        {
            idx = i;
            found = true;
            break;
        }
    }

    size_t next_idx = 0;
    if (found)
    {
        next_idx = (idx + 1) % s_library_order.size();
    }

    next_id = s_library_order[next_idx];
    xSemaphoreGive(s_library_mutex);

    if (next_id.empty())
    {
        server_bsp_set_current_photo_id_internal("");
        return ESP_ERR_NOT_FOUND;
    }

    server_bsp_set_current_photo_id_internal(next_id.c_str());
    return ESP_OK;
}

// Static web UI is served from the SD card under:
//   /sdcard/web-app/
// Repository copy lives under:
//   sd-content/web-app/

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

// Slideshow settings API
esp_err_t get_slideshow_callback(httpd_req_t *req);
esp_err_t post_slideshow_callback(httpd_req_t *req);

// Photo management API
esp_err_t get_photos_callback(httpd_req_t *req);
esp_err_t get_photos_file_callback(httpd_req_t *req);
esp_err_t post_photos_select_callback(httpd_req_t *req);
esp_err_t post_photos_next_callback(httpd_req_t *req);
esp_err_t post_photos_delete_callback(httpd_req_t *req);
esp_err_t post_photos_reorder_callback(httpd_req_t *req);
esp_err_t post_photos_upload_callback(httpd_req_t *req);

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

void server_bsp_init_state(void)
{
    if (s_state_initialized)
    {
        return;
    }

    if (!s_library_mutex)
    {
        s_library_mutex = xSemaphoreCreateMutex();
    }

    // Ensure SD layout exists.
    server_bsp_ensure_dir("/sdcard/user");
    server_bsp_ensure_dir(kUserPhotoDir);
    server_bsp_ensure_dir(kFallbackDir);

    server_bsp_load_state_from_nvs();

    // Load or build library.json
    if (s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        server_bsp_refresh_library_from_sd_locked();
        xSemaphoreGive(s_library_mutex);
    }

    // If we only have legacy state, derive current photo ID.
    server_bsp_set_current_photo_id_from_legacy_path_if_needed();

    // If still no current photo, pick the first in order (if any).
    char cur_id[64] = {0};
    {
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
    }

    bool cur_ok = false;
    if (cur_id[0] != '\0' && s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        cur_ok = (server_bsp_find_photo_locked(cur_id) != nullptr);
        xSemaphoreGive(s_library_mutex);
    }

    if (!cur_ok)
    {
        char first_id[64] = {0};
        if (s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
        {
            if (!s_library_order.empty())
            {
                const std::string &id0 = s_library_order[0];
                snprintf(first_id, sizeof(first_id), "%.*s", (int)sizeof(first_id) - 1, id0.c_str());
            }
            xSemaphoreGive(s_library_mutex);
        }

        if (first_id[0] != '\0')
        {
            server_bsp_set_current_photo_id_internal(first_id);
        }
        else
        {
            server_bsp_set_current_photo_id_internal("");
        }
    }
    else
    {
        // Ensure current-image path is consistent with current rotation.
        server_bsp_update_current_image_for_rotation();
    }

    s_state_initialized = true;
}

void http_server_init(void)
{
    server_groups = xEventGroupCreate();
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // We register more than the HTTPD_DEFAULT_CONFIG() handler limit.
    // If this stays too low, later registrations will fail and uploads will 404.
    config.max_uri_handlers = 24;
    config.uri_match_fn = httpd_uri_match_wildcard; /*Wildcard enabling*/
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    server_bsp_mark_activity_internal();

    server_bsp_init_state();

    // Rotation settings API
    httpd_uri_t uri_rot = {};
    uri_rot.uri = "/api/rotation";
    uri_rot.user_ctx = NULL;
    uri_rot.method = HTTP_GET;
    uri_rot.handler = get_rotation_callback;
    httpd_register_uri_handler(server, &uri_rot);

    uri_rot.method = HTTP_POST;
    uri_rot.handler = post_rotation_callback;
    httpd_register_uri_handler(server, &uri_rot);

    // Slideshow settings API
    httpd_uri_t uri_slide = {};
    uri_slide.uri = "/api/slideshow";
    uri_slide.user_ctx = NULL;
    uri_slide.method = HTTP_GET;
    uri_slide.handler = get_slideshow_callback;
    httpd_register_uri_handler(server, &uri_slide);

    uri_slide.method = HTTP_POST;
    uri_slide.handler = post_slideshow_callback;
    httpd_register_uri_handler(server, &uri_slide);

    // Photo management API
    httpd_uri_t uri_photos = {};
    uri_photos.user_ctx = NULL;

    uri_photos.uri = "/api/photos";
    uri_photos.method = HTTP_GET;
    uri_photos.handler = get_photos_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/file/*";
    uri_photos.method = HTTP_GET;
    uri_photos.handler = get_photos_file_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/select";
    uri_photos.method = HTTP_POST;
    uri_photos.handler = post_photos_select_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/next";
    uri_photos.method = HTTP_POST;
    uri_photos.handler = post_photos_next_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/delete";
    uri_photos.method = HTTP_POST;
    uri_photos.handler = post_photos_delete_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/reorder";
    uri_photos.method = HTTP_POST;
    uri_photos.handler = post_photos_reorder_callback;
    httpd_register_uri_handler(server, &uri_photos);

    uri_photos.uri = "/api/photos/upload*";
    uri_photos.method = HTTP_POST;
    uri_photos.handler = post_photos_upload_callback;
    httpd_register_uri_handler(server, &uri_photos);

    httpd_uri_t uri_post = {};
    uri_post.uri = "/dataUP";
    uri_post.method = HTTP_POST;
    uri_post.handler = post_dataup_callback;
    uri_post.user_ctx = NULL;
    httpd_register_uri_handler(server, &uri_post);

    // Static file server from SD card (Vue build output under /sdcard/web-app).
    httpd_uri_t uri_static = {};
    uri_static.uri = "/*";
    uri_static.method = HTTP_GET;
    uri_static.handler = get_static_callback;
    uri_static.user_ctx = NULL;
    httpd_register_uri_handler(server, &uri_static);
}

static const char *server_bsp_content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
    {
        return "application/octet-stream";
    }

    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0)
    {
        return "text/html";
    }
    if (strcmp(ext, ".js") == 0)
    {
        return "application/javascript";
    }
    if (strcmp(ext, ".css") == 0)
    {
        return "text/css";
    }
    if (strcmp(ext, ".json") == 0)
    {
        return "application/json";
    }
    if (strcmp(ext, ".png") == 0)
    {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)
    {
        return "image/jpeg";
    }
    if (strcmp(ext, ".svg") == 0)
    {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".ico") == 0)
    {
        return "image/x-icon";
    }

    return "application/octet-stream";
}

static esp_err_t server_bsp_send_sd_file(httpd_req_t *req, const char *sd_path)
{
    char *resp_str = (char *)heap_caps_malloc(SEND_LEN_MAX + 1, MALLOC_CAP_SPIRAM);
    if (!resp_str)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    size_t off = 0;
    size_t len = sdcard_read_offset(sd_path, resp_str, SEND_LEN_MAX, off);
    if (len == 0)
    {
        heap_caps_free(resp_str);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found on SD");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Serving SD file: %s", sd_path);

    while (len)
    {
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
    if (strstr(uri, "..") != NULL)
    {
        return false;
    }
    if (strchr(uri, '\\') != NULL)
    {
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
    if (uri_len == 1 && uri[0] == '/')
    {
        snprintf(out_path, out_path_len, "%s/index.html", kSdWebRoot);
        return;
    }

    // Copy the path part into a small buffer so we can check trailing '/'.
    char tmp[128] = {0};
    snprintf(tmp, sizeof(tmp), "%.*s", (int)uri_len, uri);

    if (tmp[0] == '\0')
    {
        snprintf(out_path, out_path_len, "%s/index.html", kSdWebRoot);
        return;
    }

    // Directory -> append index.html
    const size_t tlen = strlen(tmp);
    if (tlen > 0 && tmp[tlen - 1] == '/')
    {
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

    if (!server_bsp_uri_is_safe(uri))
    {
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

esp_err_t get_slideshow_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const bool enabled = server_bsp_get_slideshow_enabled();
    const uint32_t interval_s = server_bsp_get_slideshow_interval_s();

    char resp[128] = {0};
    snprintf(resp, sizeof(resp), "{\"enabled\":%s,\"interval_s\":%u}\n",
             enabled ? "true" : "false", (unsigned)interval_s);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_slideshow_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    char body[256] = {0};
    const esp_err_t body_err = server_bsp_recv_small_body(req, body, sizeof(body));
    if (body_err == ESP_ERR_INVALID_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_OK;
    }
    if (body_err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    const cJSON *jen = cJSON_GetObjectItem(root, "enabled");
    const cJSON *jint = cJSON_GetObjectItem(root, "interval_s");

    // Default missing fields to the current saved values so clients can PATCH-like update.
    bool enabled = server_bsp_get_slideshow_enabled();
    uint32_t interval_s = server_bsp_get_slideshow_interval_s();

    if (jen)
    {
        enabled = cJSON_IsTrue(jen) || (cJSON_IsNumber(jen) && jen->valuedouble != 0);
    }

    if (jint)
    {
        if (cJSON_IsNumber(jint))
        {
            interval_s = (uint32_t)jint->valuedouble;
        }
        else if (cJSON_IsString(jint) && jint->valuestring)
        {
            char *end = NULL;
            const unsigned long v = strtoul(jint->valuestring, &end, 10);
            if (end == jint->valuestring || (end && *end != '\0'))
            {
                cJSON_Delete(root);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid interval_s");
                return ESP_OK;
            }
            interval_s = (uint32_t)v;
        }
    }

    cJSON_Delete(root);

    esp_err_t err = server_bsp_set_slideshow(enabled, interval_s);
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slideshow settings");
        return ESP_OK;
    }

    // Reply with the saved settings.
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[128] = {0};
    snprintf(resp, sizeof(resp), "{\"enabled\":%s,\"interval_s\":%u}\n",
             server_bsp_get_slideshow_enabled() ? "true" : "false",
             (unsigned)server_bsp_get_slideshow_interval_s());
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void server_bsp_trim_in_place(char *s)
{
    if (!s)
    {
        return;
    }

    // Trim leading whitespace.
    char *start = s;
    while (*start && isspace((unsigned char)*start))
    {
        start++;
    }
    if (start != s)
    {
        memmove(s, start, strlen(start) + 1);
    }

    // Trim trailing whitespace.
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
    {
        s[len - 1] = '\0';
        len--;
    }
}

static bool server_bsp_photo_name_is_safe(const char *name)
{
    if (!name || name[0] == '\0')
    {
        return false;
    }

    // Keep names bounded so we can safely store them in fixed-size buffers elsewhere.
    if (strlen(name) >= 128)
    {
        return false;
    }
    if (name[0] == '.')
    {
        return false;
    }
    if (strstr(name, "..") != NULL)
    {
        return false;
    }
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
    {
        return false;
    }
    if (!server_bsp_ends_with_ignore_case(name, ".bmp"))
    {
        return false;
    }

    for (const char *p = name; *p; p++)
    {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok)
        {
            return false;
        }
    }

    return true;
}

static esp_err_t server_bsp_recv_small_body(httpd_req_t *req, char *body, size_t body_size)
{
    if (!body || body_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    body[0] = '\0';

    size_t remaining = req->content_len;
    size_t off = 0;

    while (remaining > 0)
    {
        const size_t can_read = body_size - 1 - off;
        if (can_read == 0)
        {
            return ESP_ERR_INVALID_SIZE;
        }

        int ret = httpd_req_recv(req, body + off, MIN(remaining, can_read));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            return ESP_FAIL;
        }

        off += (size_t)ret;
        remaining -= (size_t)ret;
        server_bsp_mark_activity_internal();
    }

    body[off] = '\0';
    server_bsp_trim_in_place(body);
    return ESP_OK;
}

esp_err_t get_photos_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    server_bsp_ensure_library_loaded();

    char cur_id[64] = {0};
    {
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
        if (!server_bsp_photo_id_is_safe(cur_id))
        {
            cur_id[0] = '\0';
        }
    }

    const char *displaying = "";
    const char *cur_path = server_bsp_get_current_image_path();
    const char *base = server_bsp_basename(cur_path);
    if (base && server_bsp_photo_name_is_safe(base))
    {
        displaying = base;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }

    cJSON_AddNumberToObject(root, "rotation", (double)server_bsp_get_rotation());
    cJSON_AddStringToObject(root, "current", cur_id);
    cJSON_AddStringToObject(root, "displaying", displaying);

    cJSON *arr = cJSON_AddArrayToObject(root, "photos");
    unsigned count = 0;

    if (arr && s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        for (const auto &id : s_library_order)
        {
            LibraryPhoto *p = server_bsp_find_photo_locked(id.c_str());
            if (!p)
            {
                continue;
            }

            cJSON *item = cJSON_CreateObject();
            if (!item)
            {
                continue;
            }
            cJSON_AddStringToObject(item, "id", p->id.c_str());
            cJSON_AddStringToObject(item, "landscape", p->landscape.c_str());
            cJSON_AddStringToObject(item, "portrait", p->portrait.c_str());
            cJSON_AddItemToArray(arr, item);
            count++;
        }
        xSemaphoreGive(s_library_mutex);
    }

    cJSON_AddNumberToObject(root, "count", (double)count);

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!text)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Encode error");
        return ESP_OK;
    }

    httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return ESP_OK;
}

esp_err_t get_photos_file_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    // Extract filename from URI: /api/photos/file/filename.bmp -> filename.bmp
    const char *uri = req->uri;
    const char *prefix = "/api/photos/file/";
    const size_t prefix_len = strlen(prefix);

    if (strncmp(uri, prefix, prefix_len) != 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid URI");
        return ESP_OK;
    }

    const char *filename = uri + prefix_len;

    // Validate filename (no path traversal, must end with .bmp)
    if (!server_bsp_photo_name_is_safe(filename))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_OK;
    }

    // Build full path to the photo file
    char sd_path[256] = {0};
    snprintf(sd_path, sizeof(sd_path), "%s/%s", kUserPhotoDir, filename);

    // Check if file exists
    struct stat st = {};
    if (stat(sd_path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Photo file not found");
        return ESP_OK;
    }

    // Set content type for BMP
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");

    // Serve the file
    return server_bsp_send_sd_file(req, sd_path);
}

esp_err_t post_photos_select_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    char id[64] = {0};
    const esp_err_t body_err = server_bsp_recv_small_body(req, id, sizeof(id));
    if (body_err == ESP_ERR_INVALID_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_OK;
    }
    if (body_err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        return ESP_OK;
    }

    if (!server_bsp_photo_id_is_safe(id))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid photo id");
        return ESP_OK;
    }

    server_bsp_ensure_library_loaded();

    bool exists = false;
    if (s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        exists = (server_bsp_find_photo_locked(id) != nullptr);
        xSemaphoreGive(s_library_mutex);
    }

    if (!exists)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Photo not found");
        return ESP_OK;
    }

    server_bsp_set_current_photo_id_internal(id);

    // Re-display selected image.
    xEventGroupSetBits(server_groups, set_bit_button(2));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[256] = {0};
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"current\":\"%s\"}\n", id);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_photos_next_callback(httpd_req_t *req)
{
    (void)req;
    server_bsp_mark_activity_internal();

    esp_err_t err = server_bsp_select_next_photo();
    if (err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No photos");
        return ESP_OK;
    }

    // Re-display newly selected image.
    xEventGroupSetBits(server_groups, set_bit_button(2));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char cur_id[64] = {0};
    {
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
        if (!server_bsp_photo_id_is_safe(cur_id))
        {
            cur_id[0] = '\0';
        }
    }

    char resp[256] = {0};
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"current\":\"%s\"}\n", cur_id);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_photos_delete_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    char id[64] = {0};
    const esp_err_t body_err = server_bsp_recv_small_body(req, id, sizeof(id));
    if (body_err == ESP_ERR_INVALID_SIZE)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_OK;
    }
    if (body_err != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        return ESP_OK;
    }

    if (!server_bsp_photo_id_is_safe(id))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid photo id");
        return ESP_OK;
    }

    server_bsp_ensure_library_loaded();

    bool deleting_current = false;
    {
        char cur_id[64] = {0};
        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        portEXIT_CRITICAL(&s_state_mux);
        deleting_current = (cur_id[0] != '\0' && strcmp(cur_id, id) == 0);
    }

    std::string land_name;
    std::string port_name;
    bool found = false;

    if (s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
    {
        LibraryPhoto *p = server_bsp_find_photo_locked(id);
        if (p)
        {
            found = true;
            land_name = p->landscape;
            port_name = p->portrait;

            // Remove from order
            std::vector<std::string> new_order;
            new_order.reserve(s_library_order.size());
            for (const auto &oid : s_library_order)
            {
                if (oid != id)
                {
                    new_order.push_back(oid);
                }
            }
            s_library_order.swap(new_order);

            // Remove from photos
            for (auto it = s_library_photos.begin(); it != s_library_photos.end(); ++it)
            {
                if (it->id == id)
                {
                    s_library_photos.erase(it);
                    break;
                }
            }

            (void)server_bsp_write_library_to_sd_locked();
        }
        xSemaphoreGive(s_library_mutex);
    }

    if (!found)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Photo not found");
        return ESP_OK;
    }

    // Best-effort delete of variant files.
    if (!land_name.empty())
    {
        char full[192] = {0};
        snprintf(full, sizeof(full), "%s/%s", kUserPhotoDir, land_name.c_str());
        (void)remove(full);
    }
    if (!port_name.empty() && port_name != land_name)
    {
        char full[192] = {0};
        snprintf(full, sizeof(full), "%s/%s", kUserPhotoDir, port_name.c_str());
        (void)remove(full);
    }

    if (deleting_current)
    {
        // Pick a new current photo (or fallback).
        server_bsp_set_current_photo_id_internal("");
        (void)server_bsp_select_next_photo();
        xEventGroupSetBits(server_groups, set_bit_button(2));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[128] = {0};
    snprintf(resp, sizeof(resp), "{\"ok\":true}\n");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_photos_reorder_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();

    if (req->content_len > (32 * 1024))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_OK;
    }

    // Read body
    const size_t want = (size_t)req->content_len;
    char *body = (char *)malloc(want + 1);
    if (!body)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    size_t remaining = want;
    size_t off = 0;
    while (remaining > 0)
    {
        int ret = httpd_req_recv(req, body + off, MIN(remaining, want - off));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            free(body);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_OK;
        }
        off += (size_t)ret;
        remaining -= (size_t)ret;
        server_bsp_mark_activity_internal();
    }
    body[off] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);

    if (!root)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    const cJSON *order = cJSON_GetObjectItem(root, "order");
    if (!order || !cJSON_IsArray(order))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'order' array");
        return ESP_OK;
    }

    server_bsp_ensure_library_loaded();

    if (!s_library_mutex || xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Lock failed");
        return ESP_OK;
    }

    std::vector<std::string> new_order;

    const int n = cJSON_GetArraySize(order);
    for (int i = 0; i < n; i++)
    {
        const cJSON *jid = cJSON_GetArrayItem(order, i);
        if (!jid || !cJSON_IsString(jid) || !jid->valuestring)
        {
            continue;
        }

        const char *id = jid->valuestring;
        if (!server_bsp_photo_id_is_safe(id))
        {
            continue;
        }

        if (server_bsp_find_photo_locked(id) == nullptr)
        {
            continue;
        }

        bool already = false;
        for (const auto &x : new_order)
        {
            if (x == id)
            {
                already = true;
                break;
            }
        }
        if (!already)
        {
            new_order.push_back(id);
        }
    }

    // Append any missing IDs at the end (preserve previous relative order).
    for (const auto &id : s_library_order)
    {
        bool already = false;
        for (const auto &x : new_order)
        {
            if (x == id)
            {
                already = true;
                break;
            }
        }
        if (!already)
        {
            new_order.push_back(id);
        }
    }

    s_library_order.swap(new_order);
    (void)server_bsp_write_library_to_sd_locked();

    xSemaphoreGive(s_library_mutex);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, "{\"ok\":true}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static bool server_bsp_allocate_new_photo_id(char *out_id, size_t out_id_len)
{
    if (!out_id || out_id_len == 0)
    {
        return false;
    }

    uint32_t seq = 0;
    nvs_handle_t nvs = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) == ESP_OK)
    {
        nvs_get_u32(nvs, kNvsKeyPhotoSeq, &seq);
        seq++;
        nvs_set_u32(nvs, kNvsKeyPhotoSeq, seq);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    else
    {
        seq = (uint32_t)esp_timer_get_time();
    }

    snprintf(out_id, out_id_len, "img_%06u", (unsigned)seq);
    return server_bsp_photo_id_is_safe(out_id);
}

esp_err_t post_photos_upload_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();
    server_bsp_init_state();

    char variant[16] = {0};
    char id[64] = {0};
    bool has_id = false;

    const size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1)
    {
        char *qstr = (char *)malloc(qlen);
        if (qstr)
        {
            if (httpd_req_get_url_query_str(req, qstr, qlen) == ESP_OK)
            {
                (void)httpd_query_key_value(qstr, "variant", variant, sizeof(variant));
                if (httpd_query_key_value(qstr, "id", id, sizeof(id)) == ESP_OK)
                {
                    has_id = true;
                }
            }
            free(qstr);
        }
    }

    const bool is_landscape = (strcmp(variant, "landscape") == 0);
    const bool is_portrait = (strcmp(variant, "portrait") == 0);
    if (!is_landscape && !is_portrait)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing/invalid variant (landscape|portrait)");
        return ESP_OK;
    }

    bool is_new = false;
    if (has_id)
    {
        server_bsp_trim_in_place(id);
        if (!server_bsp_photo_id_is_safe(id))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid photo id");
            return ESP_OK;
        }
    }
    else
    {
        if (!server_bsp_allocate_new_photo_id(id, sizeof(id)))
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate id");
            return ESP_OK;
        }
        is_new = true;
    }

    char filename[128] = {0};
    if (is_landscape)
    {
        snprintf(filename, sizeof(filename), "%s_L_r0.bmp", id);
    }
    else
    {
        snprintf(filename, sizeof(filename), "%s_P_r90.bmp", id);
    }

    if (!server_bsp_photo_name_is_safe(filename))
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bad generated filename");
        return ESP_OK;
    }

    char photo_path[192] = {0};
    snprintf(photo_path, sizeof(photo_path), "%s/%s", kUserPhotoDir, filename);

    // If caller provided an ID, it must already exist in the library.
    if (!is_new)
    {
        server_bsp_ensure_library_loaded();
        bool exists = false;
        if (s_library_mutex && xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) == pdTRUE)
        {
            exists = (server_bsp_find_photo_locked(id) != nullptr);
            xSemaphoreGive(s_library_mutex);
        }
        if (!exists)
        {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown photo id");
            return ESP_OK;
        }
    }

    // Write body to SD.
    char *buf = (char *)heap_caps_malloc(READ_LEN_MAX + 1, MALLOC_CAP_SPIRAM);
    if (!buf)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_OK;
    }

    size_t sdcard_len = 0;
    size_t remaining = req->content_len;

    xEventGroupSetBits(server_groups, set_bit_button(0));
    sdcard_write_offset(photo_path, NULL, 0, 0);

    while (remaining > 0)
    {
        int ret = httpd_req_recv(req, buf, MIN(remaining, READ_LEN_MAX));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue;
            }
            heap_caps_free(buf);
            xEventGroupSetBits(server_groups, set_bit_button(3));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_OK;
        }

        const size_t wr = sdcard_write_offset(photo_path, buf, ret, 1);
        sdcard_len += wr;
        remaining -= (size_t)ret;
        server_bsp_mark_activity_internal();
    }

    heap_caps_free(buf);
    xEventGroupSetBits(server_groups, set_bit_button(1));

    if (sdcard_len != req->content_len)
    {
        xEventGroupSetBits(server_groups, set_bit_button(3));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        return ESP_OK;
    }

    // Persist into library.json only after the file write succeeds.
    if (!s_library_mutex || xSemaphoreTake(s_library_mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Lock failed");
        return ESP_OK;
    }

    LibraryPhoto *p = server_bsp_get_or_create_photo_locked(id);
    if (!p)
    {
        xSemaphoreGive(s_library_mutex);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Library update failed");
        return ESP_OK;
    }

    if (is_landscape)
    {
        p->landscape = filename;
    }
    else
    {
        p->portrait = filename;
    }

    if (is_new)
    {
        s_library_order.push_back(id);
    }

    (void)server_bsp_write_library_to_sd_locked();
    xSemaphoreGive(s_library_mutex);

    // Decide whether to switch the display to this photo now.
    // If the upload client sends landscape+portrait in two requests, we only switch
    // immediately when the uploaded variant matches the frame's current orientation.
    const bool want_portrait = (server_bsp_get_rotation() == 90 || server_bsp_get_rotation() == 270);
    const bool uploaded_matches_orientation = (want_portrait && is_portrait) || (!want_portrait && is_landscape);

    bool should_redraw = false;

    if (is_new)
    {
        if (uploaded_matches_orientation)
        {
            // Preferred variant arrived first; show it right away.
            server_bsp_set_current_photo_id_internal(id);
            should_redraw = true;
        }
        else
        {
            // Not the preferred variant for the current orientation; remember this ID
            // and wait for the other variant upload before switching the display.
            portENTER_CRITICAL(&s_state_mux);
            snprintf(s_pending_new_photo_id, sizeof(s_pending_new_photo_id), "%.*s", (int)sizeof(s_pending_new_photo_id) - 1, id);
            portEXIT_CRITICAL(&s_state_mux);
        }
    }
    else
    {
        char cur_id[64] = {0};
        char pending_id[64] = {0};

        portENTER_CRITICAL(&s_state_mux);
        snprintf(cur_id, sizeof(cur_id), "%.*s", (int)sizeof(cur_id) - 1, s_current_photo_id);
        snprintf(pending_id, sizeof(pending_id), "%.*s", (int)sizeof(pending_id) - 1, s_pending_new_photo_id);
        portEXIT_CRITICAL(&s_state_mux);

        if (strcmp(cur_id, id) == 0)
        {
            // Current photo got an updated variant; pick the correct one for rotation.
            server_bsp_update_current_image_for_rotation();
            should_redraw = true;
        }
        else if (pending_id[0] != '\0' && strcmp(pending_id, id) == 0)
        {
            // Second half of a new upload just arrived; now we can switch and display.
            server_bsp_set_current_photo_id_internal(id);
            should_redraw = true;
        }
    }

    if (should_redraw)
    {
        xEventGroupSetBits(server_groups, set_bit_button(2));
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char resp[256] = {0};
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"id\":\"%s\",\"variant\":\"%s\",\"filename\":\"%s\"}\n",
             id, variant, filename);
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t post_dataup_callback(httpd_req_t *req)
{
    server_bsp_mark_activity_internal();
    char *buf = (char *)heap_caps_malloc(READ_LEN_MAX + 1, MALLOC_CAP_SPIRAM);
    size_t sdcard_len = 0;
    size_t remaining = req->content_len;
    const char *uri = req->uri;
    size_t ret;
    ESP_LOGI("TAG", "用户POST的URI是:%s,字节:%d", uri, remaining);
    xEventGroupSetBits(server_groups, set_bit_button(0));

    // Generate a unique photo filename under /sdcard/user/current-img/
    const uint16_t rot = server_bsp_get_rotation();

    uint32_t seq = 0;
    {
        nvs_handle_t nvs = 0;
        if (nvs_open(kNvsNamespace, NVS_READWRITE, &nvs) == ESP_OK)
        {
            nvs_get_u32(nvs, kNvsKeyPhotoSeq, &seq);
            seq++;
            nvs_set_u32(nvs, kNvsKeyPhotoSeq, seq);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        else
        {
            seq = (uint32_t)esp_timer_get_time();
        }
    }

    char photo_path[192] = {0};
    snprintf(photo_path, sizeof(photo_path), "%s/img_%06u_r%u.bmp", kUserPhotoDir, (unsigned)seq, (unsigned)rot);

    sdcard_write_offset(photo_path, NULL, 0, 0);
    while (remaining > 0)
    {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, READ_LEN_MAX))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }
        size_t req_len = sdcard_write_offset(photo_path, buf, ret, 1);
        sdcard_len += req_len; // Final comparison result
        remaining -= ret;      // Subtract the data that has already been received
        server_bsp_mark_activity_internal();
    }
    xEventGroupSetBits(server_groups, set_bit_button(1));
    if (sdcard_len == req->content_len)
    {
        // Current image becomes the newly uploaded photo.
        server_bsp_set_current_image_internal(photo_path, rot);

        httpd_resp_send_chunk(req, "上传成功", strlen("上传成功"));
        xEventGroupSetBits(server_groups, set_bit_button(2));
    }
    else
    {
        httpd_resp_send_chunk(req, "上传失败", strlen("上传失败"));
        xEventGroupSetBits(server_groups, set_bit_button(3));
    }
    httpd_resp_send_chunk(req, NULL, 0);

    heap_caps_free(buf);
    buf = NULL;
    return ESP_OK;
}

/*wifi ap init*/
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        server_bsp_mark_activity_internal();
        xEventGroupSetBits(server_groups, set_bit_button(4));
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        server_bsp_mark_activity_internal();
        xEventGroupSetBits(server_groups, set_bit_button(5));
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED)
    {
        server_bsp_mark_activity_internal();
    }
}

void Network_wifi_ap_init(void)
{
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
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", EXAMPLE_ESP_WIFI_SSID);
    snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", EXAMPLE_ESP_WIFI_PASS);
    wifi_config.ap.channel = EXAMPLE_ESP_WIFI_CHANNEL;
    wifi_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI("network", "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}

void set_espWifi_sleep(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(500));
}