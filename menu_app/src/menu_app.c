#define DBG_TAG "menu_app"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "menu_page_definitions.h"

#include <string.h>

#define MENU_PAGE_MOTION     "motion"
#define MENU_PAGE_AUDIO      "audio"
#define MENU_PAGE_STORAGE    "storage"
#define MENU_PAGE_CLOCK      "clock"

typedef struct menu_page_state
{
    app_ui_page_t page;
} menu_page_state_t;

_Static_assert(sizeof(menu_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Menu page state exceeds the lifecycle arena slot");

static void _menu_open_page(lv_event_t *event)
{
    const char *page_id = lv_event_get_user_data(event);
    app_ui_request_open_page(APP_MANAGER_ID_MENU, page_id);
}

static void _menu_open_app(lv_event_t *event)
{
    app_ui_request_run(lv_event_get_user_data(event));
}

static void _menu_page_build(menu_page_state_t *state)
{
    app_ui_page_create(&state->page, "演示中心", true);

    app_ui_add_section(state->page.content, "硬件实验");
    app_ui_add_action(state->page.content, LV_SYMBOL_GPS, "运动传感",
                      "实时六轴数据与倾斜指示", _menu_open_page,
                      (void *)MENU_PAGE_MOTION);
    app_ui_add_action(state->page.content, LV_SYMBOL_AUDIO, "音频",
                      "扬声器测试音与麦克风电平", _menu_open_page,
                      (void *)MENU_PAGE_AUDIO);
    app_ui_add_action(state->page.content, LV_SYMBOL_SD_CARD, "SD 存储",
                      "挂载状态、容量与安全读写自检", _menu_open_page,
                      (void *)MENU_PAGE_STORAGE);
    app_ui_add_action(state->page.content, LV_SYMBOL_BELL, "时间与 RTC",
                      "网络校时和清醒状态 Alarm", _menu_open_page,
                      (void *)MENU_PAGE_CLOCK);

    app_ui_add_section(state->page.content, "设备管理");
    app_ui_add_action(state->page.content, LV_SYMBOL_WIFI, "网络设置",
                      "手机配网与保存网络管理", _menu_open_app,
                      (void *)APP_MANAGER_ID_SETUP);
    app_ui_add_action(state->page.content, LV_SYMBOL_SETTINGS, "系统设置",
                      "显示、电源与设备信息", _menu_open_app,
                      (void *)APP_MANAGER_ID_SETTINGS);
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

static const app_manager_page_definition_t s_menu_root_definition =
{
    .handler = _menu_page_handler,
    .memory_size = sizeof(menu_page_state_t),
};

static const app_manager_page_route_t s_menu_routes[] =
{
    {
        .page_id = "root",
        .definition = &s_menu_root_definition,
        .user_data = NULL,
    },
    {
        .page_id = MENU_PAGE_MOTION,
        .definition = &menu_motion_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MENU_PAGE_AUDIO,
        .definition = &menu_audio_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MENU_PAGE_STORAGE,
        .definition = &menu_storage_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = MENU_PAGE_CLOCK,
        .definition = &menu_clock_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT(menu, APP_IMAGE_MENU_ICON, "演示中心", APP_MANAGER_ID_MENU, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_menu_routes);
