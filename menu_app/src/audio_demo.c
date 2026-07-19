#define DBG_TAG "audio_demo"
#define DBG_LVL DBG_INFO
#include "mt_log.h"

#include "app_manager.h"
#include "app_ui.h"
#include "audio_demo_adapter.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define AUDIO_DEMO_PAGE_SLOT_BYTES    2728U
#define AUDIO_DEMO_REFRESH_PERIOD_MS  100U
#define AUDIO_DEMO_COLOR_SURFACE      0x1A2024
#define AUDIO_DEMO_COLOR_TRACK        0x30393E
#define AUDIO_DEMO_COLOR_TEXT         0xF2F5F6
#define AUDIO_DEMO_COLOR_ACCENT       0x39C6C8

typedef struct audio_demo_page_state
{
    app_ui_page_t page;
    lv_obj_t *status_value;
    lv_obj_t *mic_value;
    lv_obj_t *mic_bar;
    lv_obj_t *volume_slider;
    lv_obj_t *volume_value;
    lv_obj_t *mute_value;
    lv_obj_t *mute_command;
    lv_obj_t *tone_command;
    lv_timer_t *refresh_timer;
    audio_demo_adapter_t adapter;
    audio_demo_snapshot_t rendered;
    uint32_t pending_volume_request_id;
    uint32_t pending_mute_request_id;
    uint8_t volume_draft;
    bool mute_draft;
    bool volume_editing;
} audio_demo_page_state_t;

_Static_assert(sizeof(audio_demo_page_state_t) <=
               AUDIO_DEMO_PAGE_SLOT_BYTES,
               "Audio demo page state exceeds the fixed lifecycle arena slot");

static void _audio_demo_set_controls_enabled(audio_demo_page_state_t *state,
        bool enabled, bool tone_playing)
{
    if (enabled)
    {
        lv_obj_remove_state(state->volume_slider, LV_STATE_DISABLED);
        lv_obj_remove_state(state->mute_command, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->volume_slider, LV_STATE_DISABLED);
        lv_obj_add_state(state->mute_command, LV_STATE_DISABLED);
    }
    if (enabled && !tone_playing)
    {
        lv_obj_remove_state(state->tone_command, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(state->tone_command, LV_STATE_DISABLED);
    }
}

static void _audio_demo_render_status(audio_demo_page_state_t *state,
                                      const audio_demo_snapshot_t *snapshot)
{
    const char *text = "正在启动";
    app_ui_status_t status = APP_UI_STATUS_ACCENT;
    switch (snapshot->state)
    {
    case AUDIO_DEMO_ADAPTER_RUNNING:
        if (snapshot->tone_state == AUDIO_DEMO_TONE_PLAYING)
        {
            text = "正在播放 440 Hz";
            status = APP_UI_STATUS_ACCENT;
        }
        else if (snapshot->tone_state == AUDIO_DEMO_TONE_COMPLETE)
        {
            text = "测试音播放完成";
            status = APP_UI_STATUS_SUCCESS;
        }
        else if (snapshot->tone_state == AUDIO_DEMO_TONE_CANCELLED)
        {
            text = "测试音已停止";
            status = APP_UI_STATUS_WARNING;
        }
        else if (snapshot->tone_state == AUDIO_DEMO_TONE_ERROR ||
                 snapshot->last_error != ESP_OK)
        {
            text = "音频操作失败";
            status = APP_UI_STATUS_ERROR;
        }
        else
        {
            text = "实时监听中";
            status = APP_UI_STATUS_SUCCESS;
        }
        break;
    case AUDIO_DEMO_ADAPTER_UNAVAILABLE:
        text = "音频不可用";
        status = APP_UI_STATUS_WARNING;
        break;
    case AUDIO_DEMO_ADAPTER_STOPPING:
        text = "正在停止";
        status = APP_UI_STATUS_NEUTRAL;
        break;
    case AUDIO_DEMO_ADAPTER_ERROR:
        text = "音频服务故障";
        status = APP_UI_STATUS_ERROR;
        break;
    case AUDIO_DEMO_ADAPTER_CLOSED:
        text = "已停止";
        status = APP_UI_STATUS_NEUTRAL;
        break;
    case AUDIO_DEMO_ADAPTER_STARTING:
    default:
        break;
    }
    app_ui_set_status_text(state->status_value, text, status);
}

static void _audio_demo_render_volume(audio_demo_page_state_t *state,
                                      const audio_demo_snapshot_t *snapshot)
{
    bool request_failed = false;
    if (state->pending_volume_request_id != 0U &&
            snapshot->volume_request_id == state->pending_volume_request_id)
    {
        request_failed = snapshot->volume_result != ESP_OK;
        state->pending_volume_request_id = 0U;
    }

    const bool local_value = state->volume_editing ||
                             state->pending_volume_request_id != 0U;
    const uint8_t volume = local_value ? state->volume_draft :
                           snapshot->volume_percent;
    if (!local_value)
    {
        state->volume_draft = volume;
    }
    lv_label_set_text_fmt(state->volume_value, "%u%%", (unsigned)volume);
    if (!state->volume_editing)
    {
        lv_slider_set_value(state->volume_slider, volume, LV_ANIM_OFF);
    }
    if (request_failed)
    {
        app_ui_set_status_text(state->status_value, "音量更新失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _audio_demo_render_mute(audio_demo_page_state_t *state,
                                    const audio_demo_snapshot_t *snapshot)
{
    bool request_failed = false;
    if (state->pending_mute_request_id != 0U &&
            snapshot->mute_request_id == state->pending_mute_request_id)
    {
        request_failed = snapshot->mute_result != ESP_OK;
        state->pending_mute_request_id = 0U;
    }

    lv_label_set_text(state->mute_value,
                      snapshot->muted ? "已静音" : "已开启");
    if (state->pending_mute_request_id != 0U)
    {
        lv_obj_add_state(state->mute_command, LV_STATE_DISABLED);
        app_ui_set_status_text(state->status_value,
                               state->mute_draft ? "正在静音" :
                               "正在取消静音", APP_UI_STATUS_ACCENT);
    }
    else if (request_failed)
    {
        app_ui_set_status_text(state->status_value, "静音更新失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _audio_demo_render(audio_demo_page_state_t *state,
                               const audio_demo_snapshot_t *snapshot)
{
    if (snapshot->generation == state->rendered.generation)
    {
        return;
    }
    state->rendered = *snapshot;
    _audio_demo_render_status(state, snapshot);

    lv_label_set_text_fmt(state->mic_value, "%u%%",
                          (unsigned)snapshot->mic_level_percent);
    lv_bar_set_value(state->mic_bar, snapshot->mic_level_percent,
                     LV_ANIM_ON);
    _audio_demo_render_volume(state, snapshot);
    const bool running = snapshot->state == AUDIO_DEMO_ADAPTER_RUNNING;
    _audio_demo_set_controls_enabled(
        state, running, snapshot->tone_state == AUDIO_DEMO_TONE_PLAYING);
    if (state->pending_volume_request_id != 0U)
    {
        lv_obj_add_state(state->volume_slider, LV_STATE_DISABLED);
    }
    _audio_demo_render_mute(state, snapshot);
}

static void _audio_demo_refresh(lv_timer_t *timer)
{
    audio_demo_page_state_t *state = lv_timer_get_user_data(timer);
    audio_demo_snapshot_t snapshot;
    if (audio_demo_adapter_get_snapshot(&state->adapter, &snapshot) == ESP_OK)
    {
        _audio_demo_render(state, &snapshot);
    }
}

static void _audio_demo_volume_event(lv_event_t *event)
{
    audio_demo_page_state_t *state = lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    uint8_t volume = (uint8_t)lv_slider_get_value(state->volume_slider);
    if (code == LV_EVENT_PRESSED)
    {
        state->volume_editing = true;
        state->volume_draft = volume;
        lv_label_set_text_fmt(state->volume_value, "%u%%", (unsigned)volume);
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        if (!state->volume_editing &&
                !lv_obj_has_state(state->volume_slider, LV_STATE_PRESSED))
        {
            return;
        }
        state->volume_editing = true;
        state->volume_draft = volume;
        lv_label_set_text_fmt(state->volume_value, "%u%%", (unsigned)volume);
        return;
    }
    if (code == LV_EVENT_PRESS_LOST)
    {
        state->volume_editing = false;
        if (state->pending_volume_request_id == 0U)
        {
            state->volume_draft = state->rendered.volume_percent;
            lv_slider_set_value(state->volume_slider, state->volume_draft,
                                LV_ANIM_OFF);
            lv_label_set_text_fmt(state->volume_value, "%u%%",
                                  (unsigned)state->volume_draft);
        }
        return;
    }
    if (code != LV_EVENT_RELEASED)
    {
        return;
    }

    state->volume_editing = false;
    state->volume_draft = volume;
    lv_label_set_text_fmt(state->volume_value, "%u%%", (unsigned)volume);
    uint32_t request_id = 0U;
    esp_err_t result = audio_demo_adapter_set_volume(
                           &state->adapter, volume, &request_id);
    if (result == ESP_OK)
    {
        state->pending_volume_request_id = request_id;
        lv_obj_add_state(state->volume_slider, LV_STATE_DISABLED);
    }
    if (result != ESP_OK)
    {
        state->pending_volume_request_id = 0U;
        state->volume_draft = state->rendered.volume_percent;
        lv_slider_set_value(state->volume_slider, state->volume_draft,
                            LV_ANIM_OFF);
        lv_label_set_text_fmt(state->volume_value, "%u%%",
                              (unsigned)state->volume_draft);
        app_ui_set_status_text(state->status_value, "音量更新失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _audio_demo_mute_event(lv_event_t *event)
{
    audio_demo_page_state_t *state = lv_event_get_user_data(event);
    const bool muted = !state->rendered.muted;
    uint32_t request_id = 0U;
    esp_err_t result = audio_demo_adapter_set_mute(
                           &state->adapter, muted, &request_id);
    if (result == ESP_OK)
    {
        state->pending_mute_request_id = request_id;
        state->mute_draft = muted;
        app_ui_set_status_text(state->status_value,
                               muted ? "正在静音" : "正在取消静音",
                               APP_UI_STATUS_ACCENT);
        lv_obj_add_state(state->mute_command, LV_STATE_DISABLED);
    }
    else
    {
        app_ui_set_status_text(state->status_value, "静音更新失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _audio_demo_tone_event(lv_event_t *event)
{
    audio_demo_page_state_t *state = lv_event_get_user_data(event);
    esp_err_t result = audio_demo_adapter_play_tone(&state->adapter);
    if (result == ESP_OK)
    {
        app_ui_set_status_text(state->status_value, "测试音已排队",
                               APP_UI_STATUS_ACCENT);
        lv_obj_add_state(state->tone_command, LV_STATE_DISABLED);
    }
    else
    {
        app_ui_set_status_text(state->status_value, "测试音启动失败",
                               APP_UI_STATUS_ERROR);
    }
}

static void _audio_demo_build_volume(audio_demo_page_state_t *state)
{
    lv_obj_t *panel = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(panel);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_height(panel, 100);
    lv_obj_set_style_bg_color(panel, lv_color_hex(AUDIO_DEMO_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_obj_create(panel);
    lv_obj_remove_style_all(heading);
    lv_obj_set_width(heading, LV_PCT(100));
    lv_obj_set_height(heading, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(heading, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(heading);
    lv_label_set_text(label, "扬声器音量");
    lv_obj_set_style_text_color(label, lv_color_hex(AUDIO_DEMO_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, app_ui_font(APP_THEME_FONT_BODY), 0);

    state->volume_value = lv_label_create(heading);
    lv_label_set_text(state->volume_value, "--");
    lv_obj_set_style_text_color(state->volume_value,
                                lv_color_hex(AUDIO_DEMO_COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(state->volume_value,
                               app_ui_font(APP_THEME_FONT_BODY), 0);

    state->volume_slider = lv_slider_create(panel);
    lv_obj_set_width(state->volume_slider, LV_PCT(100));
    lv_obj_set_height(state->volume_slider, 20);
    lv_slider_set_range(state->volume_slider, 0, 100);
    lv_obj_set_style_bg_color(state->volume_slider,
                              lv_color_hex(AUDIO_DEMO_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->volume_slider,
                              lv_color_hex(AUDIO_DEMO_COLOR_ACCENT),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->volume_slider,
                              lv_color_hex(AUDIO_DEMO_COLOR_TEXT),
                              LV_PART_KNOB);
    lv_obj_add_event_cb(state->volume_slider, _audio_demo_volume_event,
                        LV_EVENT_PRESSED, state);
    lv_obj_add_event_cb(state->volume_slider, _audio_demo_volume_event,
                        LV_EVENT_VALUE_CHANGED, state);
    lv_obj_add_event_cb(state->volume_slider, _audio_demo_volume_event,
                        LV_EVENT_RELEASED, state);
    lv_obj_add_event_cb(state->volume_slider, _audio_demo_volume_event,
                        LV_EVENT_PRESS_LOST, state);
}

static void _audio_demo_build(audio_demo_page_state_t *state)
{
    app_ui_page_create(&state->page, "音频", true);
    (void)app_ui_add_section(state->page.content, "实时输入");
    (void)app_ui_add_value_row(state->page.content, "状态", "正在启动",
                               &state->status_value);
    (void)app_ui_add_value_row(state->page.content, "麦克风电平", "0%",
                               &state->mic_value);

    state->mic_bar = lv_bar_create(state->page.content);
    lv_obj_set_width(state->mic_bar, LV_PCT(100));
    lv_obj_set_height(state->mic_bar, 12);
    lv_bar_set_range(state->mic_bar, 0, 100);
    lv_bar_set_value(state->mic_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(state->mic_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(state->mic_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(state->mic_bar,
                              lv_color_hex(AUDIO_DEMO_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->mic_bar,
                              lv_color_hex(AUDIO_DEMO_COLOR_ACCENT),
                              LV_PART_INDICATOR);

    (void)app_ui_add_section(state->page.content, "扬声器");
    (void)app_ui_add_value_row(state->page.content, "静音", "--",
                               &state->mute_value);
    _audio_demo_build_volume(state);
    state->mute_command = app_ui_add_command(
                              state->page.content, LV_SYMBOL_MUTE, "切换静音",
                              NULL, _audio_demo_mute_event, state);
    state->tone_command = app_ui_add_command(
                              state->page.content, LV_SYMBOL_PLAY, "播放 440 Hz 测试音",
                              "持续 1 秒", _audio_demo_tone_event, state);
    _audio_demo_set_controls_enabled(state, false, false);
}

static esp_err_t _audio_demo_resume(audio_demo_page_state_t *state)
{
    if (!audio_demo_adapter_is_open(&state->adapter))
    {
        esp_err_t result = audio_demo_adapter_open(&state->adapter);
        if (result != ESP_OK)
        {
            app_ui_set_status_text(state->status_value, "音频启动失败",
                                   APP_UI_STATUS_ERROR);
            return result;
        }
    }

    memset(&state->rendered, 0, sizeof(state->rendered));
    state->pending_volume_request_id = 0U;
    state->pending_mute_request_id = 0U;
    state->volume_editing = false;
    audio_demo_snapshot_t snapshot;
    if (audio_demo_adapter_get_snapshot(&state->adapter, &snapshot) == ESP_OK)
    {
        _audio_demo_render(state, &snapshot);
    }
    if (state->refresh_timer == NULL)
    {
        state->refresh_timer = lv_timer_create(
                                   _audio_demo_refresh,
                                   AUDIO_DEMO_REFRESH_PERIOD_MS, state);
    }
    return ESP_OK;
}

static esp_err_t _audio_demo_pause(audio_demo_page_state_t *state)
{
    if (state->refresh_timer != NULL)
    {
        lv_timer_delete(state->refresh_timer);
        state->refresh_timer = NULL;
    }
    (void)audio_demo_adapter_cancel_tone(&state->adapter);
    esp_err_t result = audio_demo_adapter_close(&state->adapter);
    if (result != ESP_OK)
    {
        app_manager_this_page_report_cleanup_error(result);
        LOG_W("audio worker close incomplete: %s", esp_err_to_name(result));
    }
    return result;
}

static void _audio_demo_unmount(audio_demo_page_state_t *state)
{
    app_ui_page_destroy(&state->page);
    state->status_value = NULL;
    state->mic_value = NULL;
    state->mic_bar = NULL;
    state->volume_slider = NULL;
    state->volume_value = NULL;
    state->mute_value = NULL;
    state->mute_command = NULL;
    state->tone_command = NULL;
}

static void _audio_demo_handler(app_manager_msg_type_t message, void *param)
{
    (void)param;
    audio_demo_page_state_t *state = app_manager_this_page_memory();
    switch (message)
    {
    case APP_MANAGER_MSG_ONSTART:
        memset(state, 0, sizeof(*state));
        LOG_I("started");
        break;
    case APP_MANAGER_MSG_ONMOUNT:
        if (state->page.root == NULL)
        {
            _audio_demo_build(state);
        }
        break;
    case APP_MANAGER_MSG_ONRESUME:
        (void)_audio_demo_resume(state);
        break;
    case APP_MANAGER_MSG_ONPAUSE:
        (void)_audio_demo_pause(state);
        break;
    case APP_MANAGER_MSG_ONUNMOUNT:
        _audio_demo_unmount(state);
        break;
    case APP_MANAGER_MSG_ONSTOP:
        if (_audio_demo_pause(state) == ESP_OK)
        {
            LOG_I("stopped");
        }
        break;
    default:
        break;
    }
}

APP_MANAGER_PAGE_EXPORT(menu_audio, APP_MANAGER_ID_MENU, "audio",
                        _audio_demo_handler, NULL,
                        sizeof(audio_demo_page_state_t));
