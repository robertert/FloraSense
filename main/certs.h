#pragma once

#include "esp_err.h"

extern char *server_cert_pem;
extern char *server_key_pem;

void ensure_certificates_exist();

esp_err_t save_str_to_nvs(const char *key, const char *value);
esp_err_t load_str_from_nvs(const char *key, char **out_value);