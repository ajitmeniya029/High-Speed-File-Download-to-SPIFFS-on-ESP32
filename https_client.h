#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include "esp_err.h"

// Initialize HTTPS and download file from given URL to SPIFFS
esp_err_t https_download_file(const char *url, const char *dest_path);

#endif // HTTPS_CLIENT_H