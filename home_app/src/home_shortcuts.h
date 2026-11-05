#ifndef __HOME_SHORTCUTS_H__
#define __HOME_SHORTCUTS_H__

#include "app_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of application slots in the Home quick-access dock. */
#define HOME_SHORTCUT_SLOTS 3U
/** Maximum number of eligible applications exposed to the picker page. */
#define HOME_SHORTCUT_CATALOG_MAX 24U

/** Persisted Home shortcut selection. */
typedef struct home_shortcut_prefs
{
    bool configured; /**< True after the user saves a selection. */
    uint8_t count;   /**< Number of valid entries in app_ids. */
    char app_ids[HOME_SHORTCUT_SLOTS][APP_MANAGER_ID_BYTES]; /**< App IDs. */
} home_shortcut_prefs_t;

/**
 * @brief Reset a preference structure to the built-in defaults.
 * @param prefs receives the reset value.
 */
void home_shortcuts_defaults(home_shortcut_prefs_t *prefs);

/**
 * @brief Load the persisted selection once for this App instance.
 * @return ESP_OK, or the NVS error while the in-memory defaults remain usable.
 */
esp_err_t home_shortcuts_ensure_loaded(void);

/**
 * @brief Read the current in-memory selection.
 * @return Stable App-scope preference storage.
 */
const home_shortcut_prefs_t *home_shortcuts_get(void);

/**
 * @brief Return the revision counter for UI rebuild decisions.
 */
uint32_t home_shortcuts_generation(void);

/**
 * @brief Collect the Apps eligible for the Home shortcut picker.
 *
 * Hidden Apps, Home and Menu are excluded. The result is ordered by
 * launcher_order, with zero treated as the last position.
 *
 * @param out receives descriptor pointers.
 * @param capacity is the number of slots available in out.
 * @return Number of descriptors written (never more than capacity).
 */
size_t home_shortcut_catalog(const app_manager_app_desc_t **out,
                             size_t capacity);

/**
 * @brief Resolve the dock entries for the current preference state.
 *
 * An unconfigured preference uses the first eligible non-Weather Apps in
 * catalog order. A configured preference uses its selected IDs, dropping
 * entries that no longer exist in the product.
 *
 * @param prefs is the current preference snapshot.
 * @param catalog is the ordered eligible descriptor list.
 * @param catalog_count is the number of entries in catalog.
 * @param out receives the resolved descriptors.
 * @param capacity is the number of slots available in out.
 * @return Number of descriptors written.
 */
size_t home_shortcut_effective(const home_shortcut_prefs_t *prefs,
                               const app_manager_app_desc_t *const *catalog,
                               size_t catalog_count,
                               const app_manager_app_desc_t **out,
                               size_t capacity);

/**
 * @brief Report whether an App ID is selected.
 * @param prefs is the current preference snapshot.
 * @param app_id is the stable App identifier.
 * @return true when selected.
 */
bool home_shortcuts_is_selected(const home_shortcut_prefs_t *prefs,
                                const char *app_id);

/**
 * @brief Change one selection and queue its persistent save.
 *
 * The UI worker calls this only; the NVS write is submitted to chore_service.
 *
 * @param app is the descriptor to add or remove.
 * @param selected is true to add, false to remove.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE when the third
 *         slot is already occupied, or a chore-service admission error.
 */
esp_err_t home_shortcuts_set_selected(const app_manager_app_desc_t *app,
                                      bool selected);

#ifdef __cplusplus
}
#endif

#endif /* __HOME_SHORTCUTS_H__ */
