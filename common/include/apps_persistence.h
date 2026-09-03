#ifndef __APPS_PERSISTENCE_H__
#define __APPS_PERSISTENCE_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Delete every built-in App setting persisted in NVS.
 *
 * This idempotent factory-reset operation is valid after nv_storage_init()
 * and before any built-in App starts.
 *
 * @return ESP_OK when all settings are absent, otherwise an NVS error.
 */
esp_err_t apps_factory_reset_persisted_state(void);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_PERSISTENCE_H__ */
