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


void generate_certificates() {
    ESP_LOGW(TAG, "GEN: Generowanie kluczy RSA (moze potrwac do 20s)...");
    
    int ret;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509write_cert crt;

    mbedtls_pk_init(&key);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509write_crt_init(&crt);

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"ESP_V3", 6)) != 0) {
        ESP_LOGE(TAG, "GEN: Blad DRBG");
    }

    ESP_LOGI(TAG, "GEN: Obliczam RSA...");
    if ((ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0 ||
        (ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg, 2048, 65537)) != 0) {
        ESP_LOGE(TAG, "GEN: Blad RSA -0x%x", -ret);
        return;
    }

    ESP_LOGI(TAG, "GEN: Podpisywanie...");
    const char *subject_name = "CN=ESP32,O=IoT,C=PL";

    mbedtls_x509write_crt_set_subject_name(&crt, subject_name);
    mbedtls_x509write_crt_set_issuer_name(&crt, subject_name); 
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_validity(&crt, "20230101000000", "20351231235959");

    unsigned char serial[] = {0x01}; 
    mbedtls_x509write_crt_set_serial_raw(&crt, serial, 1);

    mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1);
    mbedtls_x509write_crt_set_subject_key_identifier(&crt);
    mbedtls_x509write_crt_set_authority_key_identifier(&crt);

    size_t buf_len = 4096;
    char *key_buf = calloc(1, buf_len);
    char *crt_buf = calloc(1, buf_len);

    mbedtls_pk_write_key_pem(&key, (unsigned char*)key_buf, buf_len);
    mbedtls_x509write_crt_pem(&crt, (unsigned char*)crt_buf, buf_len, mbedtls_ctr_drbg_random, &ctr_drbg);

    printf("%s\n", crt_buf);

    ESP_LOGI(TAG, "GEN: Zapis do NVS...");
    esp_err_t e1 = save_str_to_nvs("srv_key", key_buf);
    esp_err_t e2 = save_str_to_nvs("srv_cert", crt_buf);

    if (e1 == ESP_OK && e2 == ESP_OK) {
        ESP_LOGI(TAG, "GEN: Zapis zakonczony sukcesem!");
    } else {
        ESP_LOGE(TAG, "GEN: Blad zapisu do NVS! Key: %s, Cert: %s", esp_err_to_name(e1), esp_err_to_name(e2));
    }
    
    free(key_buf);
    free(crt_buf);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509write_crt_free(&crt);
}

void ensure_certificates_exist() {
    if (load_str_from_nvs("srv_cert", &server_cert_pem) != ESP_OK || load_str_from_nvs("srv_key", &server_key_pem) != ESP_OK) 
    {
        ESP_LOGW(TAG, "Brak kluczy. Generuje nowe...");
        if (server_cert_pem) free(server_cert_pem);
        if (server_key_pem) free(server_key_pem);
        
        generate_certificates();
        load_str_from_nvs("srv_cert", &server_cert_pem);
        load_str_from_nvs("srv_key", &server_key_pem);
    }
    
    if (server_cert_pem && server_key_pem) ESP_LOGI(TAG, "Certyfikaty gotowe.");
    else ESP_LOGE(TAG, "KRYTYCZNY BLAD: Nie udalo sie zaladowac certyfikatow po wygenerowaniu!");

}

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