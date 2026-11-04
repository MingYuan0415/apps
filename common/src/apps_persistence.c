#define DBG_TAG "apps_persistence"
#include "mt_log.h"

#include "apps_persistence.h"

#if defined(MT_PRODUCT_HAS_LEVEL_APP)
    #include "level_app_persistence.h"
#endif
#if defined(MT_PRODUCT_HAS_FLYBIRDS_APP)
    #include "flybirds_app_persistence.h"
#endif

esp_err_t apps_factory_reset_persisted_state(void)
{
    esp_err_t result = ESP_OK;

#if defined(MT_PRODUCT_APP_RESET_HOOK_COUNT) && \
    MT_PRODUCT_APP_RESET_HOOK_COUNT > 0
#define MT_PRODUCT_CALL_RESET_HOOK(hook) \
        do \
        { \
            const esp_err_t hook_result = hook(); \
            if (result == ESP_OK && hook_result != ESP_OK) \
            { \
                result = hook_result; \
            } \
        } while (0);
    MT_PRODUCT_RESET_HOOKS(MT_PRODUCT_CALL_RESET_HOOK)
#undef MT_PRODUCT_CALL_RESET_HOOK
#else
#if defined(MT_PRODUCT_HAS_LEVEL_APP)
    const esp_err_t level_result = level_app_persistence_reset();
    if (result == ESP_OK && level_result != ESP_OK)
    {
        result = level_result;
    }
#endif

#if defined(MT_PRODUCT_HAS_FLYBIRDS_APP)
    const esp_err_t flybirds_result = flybirds_app_persistence_reset();
    if (result == ESP_OK && flybirds_result != ESP_OK)
    {
        result = flybirds_result;
    }
#endif
#endif

    return result;
}
