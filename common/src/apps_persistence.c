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

#if defined(MT_PRODUCT_HAS_LEVEL_APP)
    result = level_app_persistence_reset();
    if (result != ESP_OK)
    {
        return result;
    }
#endif

#if defined(MT_PRODUCT_HAS_FLYBIRDS_APP)
    result = flybirds_app_persistence_reset();
    if (result != ESP_OK)
    {
        return result;
    }
#endif

    return result;
}
