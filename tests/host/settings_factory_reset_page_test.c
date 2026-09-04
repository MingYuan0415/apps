#include "settings_factory_reset_page.h"

#include "factory_reset_service.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_OBJECT_CAPACITY 32U
#define TEST_LABEL_BYTES     256U

struct lv_obj_t
{
    bool live;
    bool button;
    bool label;
    lv_obj_t *parent;
    lv_event_cb_t callback;
    void *callback_user_data;
    lv_event_code_t callback_code;
    uint32_t state;
    uint32_t flags;
    char text[TEST_LABEL_BYTES];
};

struct lv_event_t
{
    lv_event_code_t code;
    void *user_data;
    lv_obj_t *target;
};

typedef union test_page_state
{
    max_align_t alignment;
    uint8_t bytes[APP_MANAGER_PAGE_STATE_BYTES];
} test_page_state_t;

static lv_obj_t s_screen;
static lv_obj_t s_objects[TEST_OBJECT_CAPACITY];
static esp_err_t s_request_result;
static unsigned s_request_count;

static lv_obj_t *_test_object_create(lv_obj_t *parent, bool button,
                                     bool label)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (!s_objects[index].live)
        {
            s_objects[index] = (lv_obj_t)
            {
                .live = true,
                .button = button,
                .label = label,
                .flags = label ? 0U : LV_OBJ_FLAG_CLICKABLE,
                .parent = parent,
            };
            return &s_objects[index];
        }
    }
    assert(false);
    return NULL;
}

static lv_obj_t *_test_find_live_label(const char *text)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].label &&
                strcmp(s_objects[index].text, text) == 0)
        {
            return &s_objects[index];
        }
    }
    return NULL;
}

static bool _test_has_live_label_containing(const char *text)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].label &&
                strstr(s_objects[index].text, text) != NULL)
        {
            return true;
        }
    }
    return false;
}

static lv_obj_t *_test_find_button_for_label(const char *text)
{
    lv_obj_t *label = _test_find_live_label(text);
    assert(label != NULL);
    for (lv_obj_t *object = label->parent; object != NULL;
            object = object->parent)
    {
        if (object->button)
        {
            return object;
        }
    }
    return NULL;
}

static lv_obj_t *_test_find_text_container_for_label(const char *text)
{
    lv_obj_t *label = _test_find_live_label(text);

    assert(label != NULL);
    assert(label->parent != NULL);
    assert(!label->parent->button);
    return label->parent;
}

static size_t _test_live_object_count(void)
{
    size_t count = 0U;
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live)
        {
            ++count;
        }
    }
    return count;
}

static void _test_click(lv_obj_t *button)
{
    assert(button != NULL);
    assert(button->live);
    assert(button->button);
    if ((button->state & LV_STATE_DISABLED) != 0U)
    {
        return;
    }
    assert(button->callback != NULL);
    lv_event_t event =
    {
        .code = button->callback_code,
        .user_data = button->callback_user_data,
    };
    button->callback(&event);
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_button_create(lv_obj_t *parent)
{
    return _test_object_create(parent, true, false);
}

lv_obj_t *lv_label_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, true);
}

lv_obj_t *lv_arc_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

lv_obj_t *lv_switch_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false);
}

void lv_arc_set_bg_angles(lv_obj_t *object, int32_t start, int32_t end)
{
    (void)object;
    (void)start;
    (void)end;
}

void lv_arc_set_angles(lv_obj_t *object, int32_t start, int32_t end)
{
    (void)object;
    (void)start;
    (void)end;
}

void lv_arc_set_rotation(lv_obj_t *object, int32_t rotation)
{
    (void)object;
    (void)rotation;
}

void lv_obj_remove_style(lv_obj_t *object, void *style, int selector)
{
    (void)object;
    (void)style;
    (void)selector;
}

lv_obj_t *lv_obj_get_child(lv_obj_t *object, int32_t index)
{
    int32_t seen = 0;

    for (size_t scan = 0U; scan < TEST_OBJECT_CAPACITY; ++scan)
    {
        if (s_objects[scan].live && s_objects[scan].parent == object)
        {
            if (seen == index)
            {
                return &s_objects[scan];
            }
            ++seen;
        }
    }
    return NULL;
}

void lv_obj_delete(lv_obj_t *object)
{
    if (object == NULL)
    {
        return;
    }
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].parent == object)
        {
            lv_obj_delete(&s_objects[index]);
        }
    }
    object->live = false;
}

void lv_obj_add_event_cb(lv_obj_t *object, lv_event_cb_t callback,
                         lv_event_code_t code, void *user_data)
{
    assert(object != NULL);
    assert(object->callback == NULL);
    object->callback = callback;
    object->callback_code = code;
    object->callback_user_data = user_data;
}

void lv_obj_add_state(lv_obj_t *object, uint32_t state)
{
    assert(object != NULL);
    object->state |= state;
}

void lv_obj_remove_state(lv_obj_t *object, uint32_t state)
{
    assert(object != NULL);
    object->state &= ~state;
}

bool lv_obj_has_state(const lv_obj_t *object, uint32_t state)
{
    return object != NULL && (object->state & state) != 0U;
}

bool lv_obj_is_valid(const lv_obj_t *object)
{
    return object != NULL && object->live;
}

void lv_obj_add_flag(lv_obj_t *object, uint32_t flag)
{
    assert(object != NULL);
    object->flags |= flag;
}

void lv_obj_remove_flag(lv_obj_t *object, uint32_t flag)
{
    assert(object != NULL);
    object->flags &= ~flag;
}

lv_event_code_t lv_event_get_code(lv_event_t *event)
{
    assert(event != NULL);
    return event->code;
}

void *lv_event_get_user_data(lv_event_t *event)
{
    assert(event != NULL);
    return event->user_data;
}

lv_obj_t *lv_event_get_target(lv_event_t *event)
{
    assert(event != NULL);
    return event->target;
}

lv_obj_t *lv_event_get_current_target(lv_event_t *event)
{
    return lv_event_get_target(event);
}

lv_result_t lv_obj_send_event(lv_obj_t *object, lv_event_code_t code,
                              void *param)
{
    (void)param;
    assert(object != NULL);
    if (object->callback != NULL &&
            object->callback_code == code)
    {
        lv_event_t event =
        {
            .code = code,
            .user_data = object->callback_user_data,
            .target = object,
        };
        object->callback(&event);
    }
    return LV_RESULT_OK;
}

void lv_label_set_text(lv_obj_t *label, const char *text)
{
    assert(label != NULL);
    assert(label->label);
    (void)snprintf(label->text, sizeof(label->text), "%s", text);
}

void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font,
                                int selector)
{
    (void)object;
    (void)font;
    (void)selector;
}

lv_obj_t *app_manager_this_page_screen(void)
{
    return &s_screen;
}

const lv_font_t *app_manager_get_font(app_theme_font_id_t id)
{
    (void)id;
    return LV_FONT_DEFAULT;
}

esp_err_t app_manager_navigate_async(
    const app_manager_nav_request_t *request,
    app_manager_nav_completion_cb_t completion, void *context)
{
    assert(request != NULL);
    if (completion != NULL)
    {
        completion(ESP_OK, context);
    }
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

esp_err_t factory_reset_service_request(void)
{
    ++s_request_count;
    return s_request_result;
}

int main(void)
{
    memset(&s_screen, 0, sizeof(s_screen));
    s_screen.live = true;
    memset(s_objects, 0, sizeof(s_objects));
    s_request_result = ESP_FAIL;
    s_request_count = 0U;

    const app_manager_page_definition_t *definition =
        &settings_factory_reset_page_definition;
    assert(definition->handler == NULL);
    assert(definition->ops != NULL);
    assert(definition->ops->mount != NULL);
    assert(definition->ops->unmount != NULL);
    assert(definition->memory_size > 0U);
    assert(definition->memory_size <= APP_MANAGER_PAGE_STATE_BYTES);

    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context =
    {
        .state = state.bytes,
        .screen = &s_screen,
    };

    definition->ops->mount(&context);
    assert(s_request_count == 0U);
    assert(_test_find_live_label("恢复出厂设置") != NULL);
    assert(_test_has_live_label_containing("设备绑定授权"));
    assert(_test_has_live_label_containing("蓝牙配对与 CCCD 状态"));
    assert(_test_has_live_label_containing("Wi-Fi 配置"));
    assert(_test_has_live_label_containing("本地传输状态"));

    lv_obj_t *confirm =
        _test_find_button_for_label("确认恢复出厂设置");
    assert(confirm != NULL);
    assert((confirm->state & LV_STATE_DISABLED) == 0U);
    lv_obj_t *confirm_text =
        _test_find_text_container_for_label("确认恢复出厂设置");
    assert((confirm_text->flags & LV_OBJ_FLAG_CLICKABLE) == 0U);

    _test_click(confirm);
    assert(s_request_count == 1U);
    assert((confirm->state & LV_STATE_DISABLED) == 0U);
    assert(_test_find_live_label("无法保存恢复请求，请重试") != NULL);

    s_request_result = ESP_OK;
    _test_click(confirm);
    assert(s_request_count == 2U);
    assert((confirm->state & LV_STATE_DISABLED) != 0U);
    assert(_test_find_live_label("恢复请求已受理，正在重启") != NULL);

    _test_click(confirm);
    assert(s_request_count == 2U);

    definition->ops->unmount(&context);
    assert(_test_live_object_count() == 0U);

    puts("settings factory-reset page tests passed");
    return 0;
}
