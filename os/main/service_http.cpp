#include "service_http.h"
#include "voice_assistant.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "http_svc";

static esp_err_t voice_config_get(httpd_req_t *req) {
    char url[160] = "";
    voice_assistant_get_config(url, sizeof(url));
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "api_url", url);
    cJSON_AddBoolToObject(root, "api_key_set", voice_assistant_configured());
    const char *body = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, body, strlen(body));
    cJSON_free((void *)body);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t voice_config_post(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char buf[513] = {};
    int received = 0;
    while (received < total) {
        int n = httpd_req_recv(req, buf + received, total - received);
        if (n <= 0) break;
        received += n;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
        return ESP_FAIL;
    }
    const cJSON *url = cJSON_GetObjectItem(root, "api_url");
    const cJSON *key = cJSON_GetObjectItem(root, "api_key");
    if (!cJSON_IsString(url)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "api_url required");
        return ESP_FAIL;
    }
    voice_assistant_set_config(url->valuestring,
                               cJSON_IsString(key) ? key->valuestring : NULL);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "{\"ok\":true}", 10);
    ESP_LOGI(TAG, "voice api config updated");
    return ESP_OK;
}

static esp_err_t voice_config_options(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t service_http_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    esp_err_t err;
    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd start");

    httpd_uri_t get_cfg = {.uri = "/api/v1/voice/config", .method = HTTP_GET, .handler = voice_config_get};
    httpd_uri_t post_cfg = {.uri = "/api/v1/voice/config", .method = HTTP_POST, .handler = voice_config_post};
    httpd_uri_t opt_cfg = {.uri = "/api/v1/voice/config", .method = HTTP_OPTIONS, .handler = voice_config_options};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &get_cfg), TAG, "get");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &post_cfg), TAG, "post");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &opt_cfg), TAG, "opt");
    ESP_LOGI(TAG, "http service on :%d", config.server_port);
    return ESP_OK;
}
