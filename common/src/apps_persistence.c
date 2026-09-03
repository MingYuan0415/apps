#define DBG_TAG "apps_persistence"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "apps_persistence.h"
#include "level_app_persistence.h"

esp_err_t apps_factory_reset_persisted_state(void)
{
    return level_app_persistence_reset();
}
