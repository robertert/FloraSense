#include "certs.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"

static const char *TAG = "CERTS_MGR";

char *server_cert_pem = NULL;
char *server_key_pem = NULL;


esp_err_t save_str_to_nvs(const char *key, const char *value) {
    if (key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) 
    {
        err = nvs_set_str(handle, key, value);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    return err;
}

esp_err_t load_str_from_nvs(const char *key, char **out_value) {
    if (key == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_value = NULL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    
    if (err == ESP_OK) {
        char *buffer = malloc(required_size);
        if (buffer) {
            err = nvs_get_str(handle, key, buffer, &required_size);
            if (err == ESP_OK) {
                *out_value = buffer;
            } else {
                free(buffer);
            }
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    
    nvs_close(handle);
    return err;
}