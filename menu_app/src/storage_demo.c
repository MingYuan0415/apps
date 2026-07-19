#define DBG_TAG "storage_page"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "storage_demo_adapter.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STORAGE_DEMO_PAGE_SLOT_BYTES 2728U
#define STORAGE_DEMO_POLL_MS         200U

typedef struct storage_demo_page_state
{
    app_ui_page_t page;
    lv_obj_t *status_value;
    lv_obj_t *capacity_value;
    lv_obj_t *free_value;
    lv_obj_t *self_test_value;
    lv_obj_t *detail_label;
    lv_obj_t *refresh_button;
    lv_obj_t *self_test_button;
    lv_timer_t *poll_timer;
    storage_demo_adapter_t *adapter;
    uint32_t rendered_generation;
} storage_demo_page_state_t;

_Static_assert(sizeof(storage_demo_page_state_t) <=
               STORAGE_DEMO_PAGE_SLOT_BYTES,
               "Storage demo page exceeds the fixed lifecycle arena slot");

static void _storage_demo_set_enabled(lv_obj_t *object, bool enabled)
{
    if (enabled)
    {
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(object, LV_STATE_DISABLED);
    }
}

static void _storage_demo_format_bytes(char *buffer, size_t size,
                                       uint64_t bytes)
{
    const uint64_t mib = 1024ULL * 1024ULL;
    const uint64_t gib = 1024ULL * mib;
    if (bytes >= gib)
    {
        const uint64_t hundredths = (bytes % gib) * 100ULL / gib;
        snprintf(buffer, size, "%llu.%02llu GB",
                 (unsigned long long)(bytes / gib),
                 (unsigned long long)hundredths);
    }
    else
    {
        snprintf(buffer, size, "%llu MB",
                 (unsigned long long)(bytes / mib));
    }
}

static void _storage_demo_render_self_test(
    storage_demo_page_state_t *state,
    const storage_demo_snapshot_t *snapshot)
{
    char text[48];
    switch (snapshot->self_test)
    {
    case STORAGE_DEMO_SELF_TEST_RUNNING:
        app_ui_set_status_text(state->self_test_value, "正在校验 4 KiB",
                               APP_UI_STATUS_ACCENT);
        break;
    case STORAGE_DEMO_SELF_TEST_PASSED:
        snprintf(text, sizeof(text), "通过 · 第 %lu 次",
                 (unsigned long)snapshot->self_test_count);
        app_ui_set_status_text(state->self_test_value, text,
                               APP_UI_STATUS_SUCCESS);
        break;
    case STORAGE_DEMO_SELF_TEST_FAILED:
        if (snapshot->filesystem_errno != 0)
        {
            snprintf(text, sizeof(text), "失败 · errno %d",
                     snapshot->filesystem_errno);
        }
        else
        {
            snprintf(text, sizeof(text), "失败 · 0x%x",
                     (unsigned)snapshot->last_error);
        }
        app_ui_set_status_text(state->self_test_value, text,
                               APP_UI_STATUS_ERROR);
        break;
    case STORAGE_DEMO_SELF_TEST_NOT_RUN:
    default:
        app_ui_set_status_text(state->self_test_value, "尚未运行",
                               APP_UI_STATUS_NEUTRAL);
        break;
    }
}

static void _storage_demo_render(storage_demo_page_state_t *state,
                                 const storage_demo_snapshot_t *snapshot)
{
    const bool busy = snapshot->operation != STORAGE_DEMO_OPERATION_NONE;
    if (!snapshot->ready ||
            snapshot->operation == STORAGE_DEMO_OPERATION_REFRESH)
    {
        app_ui_set_status_text(state->status_value, "正在检测",
                               APP_UI_STATUS_ACCENT);
        lv_label_set_text(state->detail_label, "正在读取 SD 卡状态");
    }
    else if (snapshot->operation == STORAGE_DEMO_OPERATION_SELF_TEST)
    {
        app_ui_set_status_text(state->status_value, "自检中",
                               APP_UI_STATUS_ACCENT);
        lv_label_set_text(state->detail_label, "请保持 SD 卡已插入");
    }
    else if (!snapshot->mounted)
    {
        app_ui_set_status_text(state->status_value, "未挂载",
                               APP_UI_STATUS_WARNING);
        lv_label_set_text(state->detail_label, "请插卡后重启设备");
    }
    else if (snapshot->last_error != ESP_OK)
    {
        app_ui_set_status_text(state->status_value, "访问失败",
                               APP_UI_STATUS_ERROR);
        lv_label_set_text(state->detail_label, "请检查 SD 卡后重试");
    }
    else
    {
        app_ui_set_status_text(state->status_value, "已挂载",
                               APP_UI_STATUS_SUCCESS);
        lv_label_set_text(state->detail_label, "/sdcard 可用");
    }

    if (snapshot->capacity_valid)
    {
        char capacity[32];
        char free_space[32];
        _storage_demo_format_bytes(capacity, sizeof(capacity),
                                   snapshot->total_bytes);
        _storage_demo_format_bytes(free_space, sizeof(free_space),
                                   snapshot->free_bytes);
        lv_label_set_text(state->capacity_value, capacity);
        lv_label_set_text(state->free_value, free_space);
    }
    else
    {
        lv_label_set_text(state->capacity_value, "--");
        lv_label_set_text(state->free_value, "--");
    }

    _storage_demo_render_self_test(state, snapshot);
    _storage_demo_set_enabled(state->refresh_button,
                              !busy && snapshot->accepting_commands);
    _storage_demo_set_enabled(state->self_test_button,
                              !busy && snapshot->mounted &&
                              snapshot->accepting_commands);
}

static void _storage_demo_poll(lv_timer_t *timer)
{
    storage_demo_page_state_t *state = lv_timer_get_user_data(timer);
    if (state->page.root == NULL || state->adapter == NULL)
    {
        return;
    }

    storage_demo_snapshot_t snapshot;
    if (storage_demo_adapter_get_snapshot(state->adapter, &snapshot) ==
            ESP_OK && snapshot.generation != state->rendered_generation)
    {
        state->rendered_generation = snapshot.generation;
        _storage_demo_render(state, &snapshot);
    }
}

static void _storage_demo_render_queue_error(
    storage_demo_page_state_t *state, esp_err_t result)
{
    if (result == ESP_ERR_INVALID_STATE)
    {
        lv_label_set_text(state->detail_label, "已有任务正在执行");
    }
    else
    {
        lv_label_set_text_fmt(state->detail_label, "请求失败 · 0x%x",
                              (unsigned)result);
    }
}

static void _storage_demo_refresh_event(lv_event_t *event)
{
    storage_demo_page_state_t *state = lv_event_get_user_data(event);
    if (state->adapter == NULL)
    {
        lv_label_set_text(state->detail_label, "存储服务不可用");
        return;
    }

    esp_err_t result = storage_demo_adapter_refresh(state->adapter);
    if (result != ESP_OK)
    {
        _storage_demo_render_queue_error(state, result);
    }
}

static void _storage_demo_self_test_event(lv_event_t *event)
{
    storage_demo_page_state_t *state = lv_event_get_user_data(event);
    if (state->adapter == NULL)
    {
        lv_label_set_text(state->detail_label, "存储服务不可用");
        return;
    }

    esp_err_t result = storage_demo_adapter_run_self_test(state->adapter);
    if (result != ESP_OK)
    {
        _storage_demo_render_queue_error(state, result);
    }
}

static void _storage_demo_page_mount(storage_demo_page_state_t *state)
{
    app_ui_page_create(&state->page, "存储", true);
    app_ui_add_section(state->page.content, "SD 卡");
    app_ui_add_value_row(state->page.content, "状态", "等待检测",
                         &state->status_value);
    app_ui_add_value_row(state->page.content, "总容量", "--",
                         &state->capacity_value);
    app_ui_add_value_row(state->page.content, "可用空间", "--",
                         &state->free_value);
    state->refresh_button = app_ui_add_command(
                                state->page.content, LV_SYMBOL_REFRESH,
                                "刷新状态", "重新读取挂载和容量",
                                _storage_demo_refresh_event, state);

    app_ui_add_section(state->page.content, "读写自检");
    app_ui_add_value_row(state->page.content, "最近结果", "尚未运行",
                         &state->self_test_value);
    state->self_test_button = app_ui_add_command(
                                  state->page.content, LV_SYMBOL_SD_CARD,
                                  "运行 4 KiB 自检", "临时文件写入与读回",
                                  _storage_demo_self_test_event, state);
    state->detail_label = app_ui_add_body_label(state->page.content,
                          "正在启动存储任务");
    _storage_demo_set_enabled(state->refresh_button, false);
    _storage_demo_set_enabled(state->self_test_button, false);
}

static void _storage_demo_page_resume(storage_demo_page_state_t *state)
{
    if (state->adapter == NULL)
    {
        esp_err_t result = storage_demo_adapter_open(&state->adapter);
        if (result != ESP_OK)
        {
            app_ui_set_status_text(state->status_value, "服务不可用",
                                   APP_UI_STATUS_ERROR);
            lv_label_set_text_fmt(state->detail_label, "启动失败 · 0x%x",
                                  (unsigned)result);
            return;
        }
    }

    storage_demo_snapshot_t snapshot;
    if (storage_demo_adapter_get_snapshot(state->adapter, &snapshot) == ESP_OK)
    {
        state->rendered_generation = snapshot.generation;
        _storage_demo_render(state, &snapshot);
    }
    if (state->poll_timer == NULL)
    {
        state->poll_timer = lv_timer_create(_storage_demo_poll,
                                            STORAGE_DEMO_POLL_MS, state);
    }
}

static esp_err_t _storage_demo_page_pause(storage_demo_page_state_t *state)
{
    if (state->poll_timer != NULL)
    {
        lv_timer_delete(state->poll_timer);
        state->poll_timer = NULL;
    }

    esp_err_t result = storage_demo_adapter_close(&state->adapter);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
        LOG_W("storage worker close incomplete: 0x%x", result);
    }
    return result;
}

static void _storage_demo_page_unmount(storage_demo_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->status_value = NULL;
    state->capacity_value = NULL;
    state->free_value = NULL;
    state->self_test_value = NULL;
    state->detail_label = NULL;
    state->refresh_button = NULL;
    state->self_test_button = NULL;
    state->rendered_generation = 0U;
}

static void _storage_demo_page_handler(app_manager_msg_type_t message,
                                       void *param)
{
    (void)param;
    storage_demo_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _storage_demo_page_mount(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        _storage_demo_page_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_storage_demo_page_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _storage_demo_page_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        if (_storage_demo_page_pause(state) == ESP_OK)
        {
            LOG_I("stopped");
        }
        break;
    default:
        break;
    }
}

APP_MANAGER_PAGE_EXPORT(menu_storage, APP_MANAGER_ID_MENU,
                        STORAGE_DEMO_PAGE_ID, _storage_demo_page_handler,
                        NULL, sizeof(storage_demo_page_state_t));
