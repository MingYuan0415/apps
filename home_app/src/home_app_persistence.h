#ifndef __HOME_APP_PERSISTENCE_H__
#define __HOME_APP_PERSISTENCE_H__

#include "home_shortcuts.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the Home shortcut selection.
 * @param prefs receives defaults when the key is absent or corrupt.
 * @return ESP_OK, or an NVS read error.
 */
esp_err_t home_app_persistence_load(home_shortcut_prefs_t *prefs);

/**
 * @brief Persist the Home shortcut selection.
 * @param prefs is the complete selection snapshot.
 * @return ESP_OK, or an NVS write error.
 */
esp_err_t home_app_persistence_save(const home_shortcut_prefs_t *prefs);

/**
 * @brief Erase the Home shortcut selection during factory reset.
 * @return ESP_OK when absent or erased, otherwise an NVS error.
 */
esp_err_t home_app_persistence_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __HOME_APP_PERSISTENCE_H__ */
