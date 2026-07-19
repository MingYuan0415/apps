#define DBG_TAG "menu_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"

#include <string.h>

typedef struct menu_page_state
{
    app_ui_page_t page;
} menu_page_state_t;

static void _menu_open_app(lv_event_t *event)
{
    app_ui_request_run(lv_event_get_user_data(event));
}

static void _menu_page_build(menu_page_state_t *state)
{
    app_ui_page_create(&state->page, "Applications", true);
    app_ui_add_section(state->page.content, "BUILT-IN");
    app_ui_add_action(state->page.content, LV_SYMBOL_HOME, "Home",
                      "Clock and device status", _menu_open_app,
                      (void *)APP_MANAGER_ID_HOME);
    app_ui_add_action(state->page.content, LV_SYMBOL_LIST, "Applications",
                      "Current app", _menu_open_app,
                      (void *)APP_MANAGER_ID_MENU);
    app_ui_add_action(state->page.content, LV_SYMBOL_SETTINGS, "Settings",
                      "Brightness, power and about", _menu_open_app,
                      (void *)APP_MANAGER_ID_SETTINGS);
    app_ui_add_action(state->page.content, LV_SYMBOL_WIFI, "Setup",
                      "WiFi network setup", _menu_open_app,
                      (void *)APP_MANAGER_ID_SETUP);
}

static void _menu_page_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    menu_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _menu_page_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        app_ui_page_destroy(&state->page);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        LOG_I("stopped");
        break;
    default:
        break;
    }
}

APP_MANAGER_APP_EXPORT(menu, NULL, APP_MANAGER_ID_MENU, "root",
                       APP_MANAGER_APP_FLAG_NONE);
APP_MANAGER_PAGE_EXPORT(menu_root, APP_MANAGER_ID_MENU, "root",
                        _menu_page_handler, NULL, sizeof(menu_page_state_t));
