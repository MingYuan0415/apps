#ifndef __SETTINGS_APP_INTERNAL_H__
#define __SETTINGS_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "connectivity_manager.h"
#include "device_link_service.h"
#include "power_service.h"
#include "sd_storage_service.h"
#include "time_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_PAGE_DISPLAY       "display"
#define SETTINGS_PAGE_DEVICE        "device"
#define SETTINGS_PAGE_POWER         "power"
#define SETTINGS_PAGE_ABOUT         "about"
#define SETTINGS_PAGE_FACTORY_RESET "factory-reset"
#define SETTINGS_PAGE_TIME          "time"
#define SETTINGS_PAGE_STORAGE       "storage"

extern const app_manager_page_definition_t settings_root_page_definition;
extern const app_manager_page_definition_t settings_display_page_definition;
extern const app_manager_page_definition_t settings_device_page_definition;
extern const app_manager_page_definition_t settings_power_page_definition;
extern const app_manager_page_definition_t settings_about_page_definition;
extern const app_manager_page_definition_t settings_time_page_definition;
extern const app_manager_page_definition_t settings_storage_page_definition;

/**
 * @brief Describe a screen timeout in words.
 * @param timeout_ms is the configured delay, or -1 for never.
 * @return Static display text.
 */
const char *settings_ui_screen_timeout_text(int32_t timeout_ms);
/**
 * @brief Describe a standby delay in words.
 * @param timeout_ms is the configured delay, or -1 for never.
 * @return Static display text.
 */
const char *settings_ui_standby_timeout_text(int32_t timeout_ms);

#endif /* __SETTINGS_APP_INTERNAL_H__ */
