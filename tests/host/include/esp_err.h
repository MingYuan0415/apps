/** @file ESP-IDF error compatibility declarations for application tests. */
#ifndef __APPS_HOST_ESP_ERR_H__
#define __APPS_HOST_ESP_ERR_H__

/** @brief Host representation of an ESP-IDF error code. */
typedef int esp_err_t;

#define ESP_OK               0
#define ESP_FAIL              (-1)
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_INVALID_RESPONSE 0x108

/** @brief Return a stable fake name for an error code. */
const char *esp_err_to_name(esp_err_t error);

#endif /* __APPS_HOST_ESP_ERR_H__ */
