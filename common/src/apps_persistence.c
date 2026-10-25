#define DBG_TAG "apps_persistence"
#include "mt_log.h"

#include "apps_persistence.h"
#include "flybirds_app_persistence.h"
#include "level_app_persistence.h"

esp_err_t apps_factory_reset_persisted_state(void)
{
    const esp_err_t level_result = level_app_persistence_reset();
    const esp_err_t flybirds_result = flybirds_app_persistence_reset();

    return level_result != ESP_OK ? level_result : flybirds_result;
}
