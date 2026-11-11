#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_OBJECT_CAPACITY 256U
#define TEST_LABEL_BYTES     128U
#define TEST_REGISTRY_MAX    32U
#define TEST_IMAGE_ID        77U

const app_manager_page_definition_t *menu_app_test_root_definition(void);

struct lv_obj_t
{
    bool live;
    bool button;
    bool label;
    bool image;
    bool has_image_recolor;
    lv_obj_t *parent;
    lv_event_cb_t callback;
    void *callback_user_data;
    lv_event_code_t callback_code;
    uint32_t state;
    uint32_t flags;
    lv_color_t image_recolor;
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
static const app_manager_app_desc_t *s_registry[TEST_REGISTRY_MAX];
static size_t s_registry_count;
static app_manager_nav_request_t s_navigation;
static char s_navigation_app_id[APP_MANAGER_ID_BYTES];
static unsigned s_navigation_count;
static bool s_return_image;
static size_t s_fail_button_create_at;
static size_t s_fail_button_create_count;
static size_t s_button_create_count;

static const app_manager_app_desc_t s_apps[] =
{
    {
        .name = "settings", .id = "settings", .root_page_id = "root",
        .display_name = "系统设置", .launcher_order = 50U,
        .launcher_subtitle = "设备与电源"
    },
    {
        .name = "home", .id = "home", .root_page_id = "root",
        .flags = APP_MANAGER_APP_FLAG_PINNED, .display_name = "主页",
        .launcher_order = 1U, .launcher_subtitle = "今日概览"
    },
    {
        .name = "weather", .id = "weather", .root_page_id = "root",
        .display_name = "天气", .launcher_order = 10U,
        .launcher_subtitle = "当前与预报"
    },
    {
        .name = "hidden", .id = "hidden", .root_page_id = "root",
        .flags = APP_MANAGER_APP_FLAG_HIDDEN, .display_name = "隐藏应用",
        .launcher_order = 15U, .launcher_subtitle = "不应显示"
    },
    {
        .name = "clock", .id = "clock", .root_page_id = "root",
        .display_name = "时钟", .launcher_order = 20U,
        .launcher_subtitle = "倒计时与专注"
    },
    {
        .name = "menu", .id = "menu", .root_page_id = "root",
        .display_name = "应用", .launcher_order = 5U,
        .launcher_subtitle = "全部功能"
    },
    {
        .name = "calculator", .id = "calculator", .root_page_id = "root",
        .display_name = "计算器", .launcher_order = 15U,
        .launcher_subtitle = NULL
    },
    {
        .name = "same_order", .id = "same_order", .root_page_id = "root",
        .display_name = "同序应用", .launcher_order = 15U,
        .launcher_subtitle = "保持注册顺序"
    },
    {
        .name = "unordered", .id = "unordered", .root_page_id = "root",
        .display_name = "未排序应用", .launcher_order = 0U,
        .launcher_subtitle = "排在最后"
    },
};

static size_t _registry_static_count(void)
{
    return sizeof(s_apps) / sizeof(s_apps[0]);
}

static lv_obj_t *_test_object_create(lv_obj_t *parent, bool button,
                                     bool label, bool image)
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
                .image = image,
                .parent = parent,
                .flags = label ? 0U : LV_OBJ_FLAG_CLICKABLE,
            };
            return &s_objects[index];
        }
    }
    assert(false);
    return NULL;
}

static lv_obj_t *_test_button_create(lv_obj_t *parent)
{
    const size_t create_index = s_button_create_count++;
    if (s_fail_button_create_count > 0U &&
            create_index >= s_fail_button_create_at &&
            create_index < s_fail_button_create_at + s_fail_button_create_count)
    {
        return NULL;
    }
    return _test_object_create(parent, true, false, false);
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

static const char *_test_row_title(size_t row_index)
{
    size_t rows = 0U;
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (!s_objects[index].live || !s_objects[index].button)
        {
            continue;
        }
        if (rows++ == row_index)
        {
            lv_obj_t *text_column = lv_obj_get_child(&s_objects[index], 1);
            lv_obj_t *title = text_column != NULL ?
                              lv_obj_get_child(text_column, 0) : NULL;
            return title != NULL && title->label ? title->text : NULL;
        }
    }
    return NULL;
}

static lv_obj_t *_test_first_recolored_image(void)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].image &&
                s_objects[index].has_image_recolor)
        {
            return &s_objects[index];
        }
    }
    return NULL;
}

static size_t _test_recolored_image_count(void)
{
    size_t count = 0U;
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].image &&
                s_objects[index].has_image_recolor)
        {
            ++count;
        }
    }
    return count;
}

static void _test_reset_faults(void)
{
    s_return_image = false;
    s_button_create_count = 0U;
    s_fail_button_create_at = 0U;
    s_fail_button_create_count = 0U;
}

static void _test_click(lv_obj_t *button)
{
    assert(button != NULL);
    assert(button->live);
    assert(button->button);
    assert(button->callback != NULL);
    lv_event_t event =
    {
        .code = button->callback_code,
        .user_data = button->callback_user_data,
        .target = button,
    };
    button->callback(&event);
}

static void _test_reset_registry(void)
{
    s_registry_count = _registry_static_count();
    assert(s_registry_count <= TEST_REGISTRY_MAX);
    for (size_t index = 0U; index < s_registry_count; ++index)
    {
        s_registry[index] = &s_apps[index];
    }
}

static app_manager_page_context_t _test_context(test_page_state_t *state)
{
    return (app_manager_page_context_t)
    {
        .state = state->bytes,
        .screen = &s_screen,
    };
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false, false);
}

lv_timer_t *lv_timer_create(void (*callback)(lv_timer_t *timer),
                            uint32_t period_ms, void *user_data)
{
    lv_timer_t *timer = calloc(1U, sizeof(*timer));
    assert(timer != NULL);
    timer->callback = callback;
    timer->period_ms = period_ms;
    timer->user_data = user_data;
    return timer;
}

void lv_timer_delete(lv_timer_t *timer)
{
    free(timer);
}

void lv_timer_pause(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        timer->paused = true;
    }
}

lv_obj_t *lv_button_create(lv_obj_t *parent)
{
    return _test_button_create(parent);
}

lv_obj_t *lv_label_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, true, false);
}

lv_obj_t *lv_image_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false, true);
}

lv_obj_t *lv_arc_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false, false);
}

lv_obj_t *lv_switch_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false, false, false);
}

void lv_obj_remove_style(lv_obj_t *object, void *style, int selector)
{
    (void)object;
    (void)style;
    (void)selector;
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

void lv_image_set_src(lv_obj_t *object, const lv_image_dsc_t *source)
{
    (void)object;
    (void)source;
}

void lv_image_set_scale(lv_obj_t *object, uint32_t scale)
{
    (void)object;
    (void)scale;
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

uint32_t lv_obj_get_child_count(const lv_obj_t *object)
{
    uint32_t count = 0U;
    for (size_t scan = 0U; scan < TEST_OBJECT_CAPACITY; ++scan)
    {
        if (s_objects[scan].live && s_objects[scan].parent == object)
        {
            ++count;
        }
    }
    return count;
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
    return event->code;
}

void *lv_event_get_user_data(lv_event_t *event)
{
    return event->user_data;
}

lv_obj_t *lv_event_get_target(lv_event_t *event)
{
    return event->target;
}

lv_obj_t *lv_event_get_current_target(lv_event_t *event)
{
    return event->target;
}

lv_result_t lv_obj_send_event(lv_obj_t *object, lv_event_code_t code,
                              void *param)
{
    (void)param;
    assert(object != NULL);
    if (object->callback != NULL && object->callback_code == code)
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

const char *lv_label_get_text(const lv_obj_t *label)
{
    assert(label != NULL);
    return label->text;
}

void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font,
                                int selector)
{
    (void)object;
    (void)font;
    (void)selector;
}

void lv_obj_set_style_image_recolor(lv_obj_t *object, lv_color_t color,
                                    int selector)
{
    (void)selector;
    assert(object != NULL);
    object->has_image_recolor = true;
    object->image_recolor = color;
}

void lv_obj_set_style_image_recolor_opa(lv_obj_t *object, int opacity,
                                        int selector)
{
    (void)object;
    (void)opacity;
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

static lv_image_dsc_t s_image;

esp_err_t app_manager_get_image(uint32_t semantic_id,
                                const lv_image_dsc_t **image)
{
    const bool found = s_return_image && semantic_id == TEST_IMAGE_ID;
    if (image != NULL)
    {
        *image = found ? &s_image : NULL;
    }
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

const app_manager_app_desc_t *app_manager_builtin_list_open(void)
{
    return s_registry_count > 0U ? s_registry[0] : NULL;
}

const app_manager_app_desc_t *app_manager_builtin_list_get_next(
    const app_manager_app_desc_t *prev)
{
    for (size_t index = 0U; index + 1U < s_registry_count; ++index)
    {
        if (s_registry[index] == prev)
        {
            return s_registry[index + 1U];
        }
    }
    return NULL;
}

esp_err_t app_manager_navigate_async(
    const app_manager_nav_request_t *request,
    app_manager_nav_completion_cb_t completion, void *context)
{
    assert(request != NULL);
    assert(!request->has_arguments);
    s_navigation = *request;
    assert(request->app_id != NULL);
    (void)snprintf(s_navigation_app_id, sizeof(s_navigation_app_id), "%s",
                   request->app_id);
    s_navigation.app_id = s_navigation_app_id;
    ++s_navigation_count;
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

static void _test_dynamic_registry(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    memset(&s_navigation, 0, sizeof(s_navigation));
    memset(s_navigation_app_id, 0, sizeof(s_navigation_app_id));
    s_navigation_count = 0U;
    _test_reset_faults();
    _test_reset_registry();

    const app_manager_page_definition_t *definition =
        menu_app_test_root_definition();
    assert(definition != NULL);
    assert(definition->ops != NULL);
    assert(definition->ops->mount != NULL);
    assert(definition->ops->unmount != NULL);
    assert(definition->memory_size <= APP_MANAGER_PAGE_STATE_BYTES);

    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _test_context(&state);
    definition->ops->mount(&context);

    assert(_test_find_live_label("应用") != NULL);
    assert(_test_find_live_label("共 6 个") != NULL);
    assert(_test_find_live_label("天气") != NULL);
    assert(_test_find_live_label("当前与预报") != NULL);
    assert(_test_find_live_label("计算器") != NULL);
    assert(_test_find_live_label("打开应用") != NULL);
    assert(_test_find_live_label("时钟") != NULL);
    assert(_test_find_live_label("系统设置") != NULL);
    assert(_test_find_live_label("主页") == NULL);
    assert(_test_find_live_label("全部功能") == NULL);
    assert(_test_find_live_label("隐藏应用") == NULL);
    assert(_test_find_live_label("不应显示") == NULL);
    assert(strcmp(_test_row_title(0U), "天气") == 0);
    assert(strcmp(_test_row_title(1U), "计算器") == 0);
    assert(strcmp(_test_row_title(2U), "同序应用") == 0);
    assert(strcmp(_test_row_title(3U), "时钟") == 0);
    assert(strcmp(_test_row_title(4U), "系统设置") == 0);
    assert(strcmp(_test_row_title(5U), "未排序应用") == 0);
    assert(_test_row_title(6U) == NULL);

    lv_obj_t *weather = _test_find_button_for_label("天气");
    _test_click(weather);
    assert(s_navigation_count == 1U);
    assert(s_navigation.operation == APP_MANAGER_NAV_OP_RUN);
    assert(strcmp(s_navigation.app_id, APP_MANAGER_ID_WEATHER) == 0);
    assert(s_navigation.page_id == NULL);

    definition->ops->unmount(&context);
    assert(_test_live_object_count() == 0U);
}

static void _test_empty_registry(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    _test_reset_faults();
    s_registry_count = 0U;
    const app_manager_page_definition_t *definition =
        menu_app_test_root_definition();
    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _test_context(&state);
    definition->ops->mount(&context);
    assert(_test_find_live_label("暂无可用应用") != NULL);
    assert(_test_find_live_label("共 0 个") == NULL);
    definition->ops->unmount(&context);
    assert(_test_live_object_count() == 0U);
}

static void _test_growth_appends_rows(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    _test_reset_faults();
    _test_reset_registry();
    const app_manager_page_definition_t *definition =
        menu_app_test_root_definition();

    test_page_state_t before_state;
    memset(&before_state, 0, sizeof(before_state));
    const app_manager_page_context_t before_context =
        _test_context(&before_state);
    definition->ops->mount(&before_context);
    const size_t before_objects = _test_live_object_count();
    assert(_test_find_live_label("共 6 个") != NULL);
    definition->ops->unmount(&before_context);

    static const app_manager_app_desc_t added =
    {
        .name = "newcomer", .id = "newcomer", .root_page_id = "root",
        .display_name = "新应用", .launcher_order = 25U,
        .launcher_subtitle = "未来新增"
    };
    assert(s_registry_count < TEST_REGISTRY_MAX);
    s_registry[s_registry_count++] = &added;

    memset(s_objects, 0, sizeof(s_objects));
    test_page_state_t after_state;
    memset(&after_state, 0, sizeof(after_state));
    const app_manager_page_context_t after_context =
        _test_context(&after_state);
    definition->ops->mount(&after_context);
    assert(_test_find_live_label("共 7 个") != NULL);
    assert(_test_find_live_label("新应用") != NULL);
    assert(_test_find_live_label("未来新增") != NULL);
    assert(_test_live_object_count() > before_objects);
    definition->ops->unmount(&after_context);
    assert(_test_live_object_count() == 0U);
}

static void _test_partial_row_failure(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    _test_reset_faults();
    _test_reset_registry();
    /* Rows are created in order, so fail only the row for "计算器". */
    s_fail_button_create_at = 1U;
    s_fail_button_create_count = 1U;

    const app_manager_page_definition_t *definition =
        menu_app_test_root_definition();
    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _test_context(&state);
    definition->ops->mount(&context);

    assert(_test_find_live_label("共 5 个") != NULL);
    assert(_test_find_live_label("天气") != NULL);
    assert(_test_find_live_label("计算器") == NULL);
    assert(_test_find_live_label("同序应用") != NULL);
    assert(_test_find_live_label("时钟") != NULL);
    assert(_test_find_live_label("系统设置") != NULL);
    assert(_test_find_live_label("未排序应用") != NULL);
    definition->ops->unmount(&context);
    assert(_test_live_object_count() == 0U);
}

static void _test_all_row_failures(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    _test_reset_faults();
    _test_reset_registry();
    s_fail_button_create_at = 0U;
    s_fail_button_create_count = 16U;

    const app_manager_page_definition_t *definition =
        menu_app_test_root_definition();
    test_page_state_t state;
    memset(&state, 0, sizeof(state));
    const app_manager_page_context_t context = _test_context(&state);
    definition->ops->mount(&context);

    assert(_test_find_live_label("应用目录加载失败") != NULL);
    assert(_test_find_live_label("共 6 个") == NULL);
    assert(_test_find_live_label("天气") == NULL);
    definition->ops->unmount(&context);
    assert(_test_live_object_count() == 0U);
}

static void _test_normal_image_is_recolored(void)
{
    memset(s_objects, 0, sizeof(s_objects));
    _test_reset_faults();
    s_return_image = true;

    lv_obj_t *group = lv_obj_create(&s_screen);
    assert(group != NULL);
    lv_obj_t *row = app_ui_group_add_nav_image(group, TEST_IMAGE_ID,
        LV_SYMBOL_IMAGE,
        "普通图标", "语义着色",
        NULL, NULL);
    assert(row != NULL);
    lv_obj_t *image = _test_first_recolored_image();
    assert(image != NULL);
    assert(image->image_recolor == APP_UI_COLOR_RAIN);

    lv_obj_t *app_row = app_ui_group_add_app_image(group, TEST_IMAGE_ID,
        LV_SYMBOL_IMAGE,
        "彩色应用", "保留原色",
        NULL, NULL);
    assert(app_row != NULL);
    assert(image->image_recolor == APP_UI_COLOR_RAIN);
    assert(_test_recolored_image_count() == 1U);

    lv_obj_t *danger_row = app_ui_group_add_danger_image(group, TEST_IMAGE_ID,
        LV_SYMBOL_IMAGE,
        "危险操作", "警示色",
        NULL, NULL);
    assert(danger_row != NULL);
    const size_t recolored = _test_recolored_image_count();
    assert(recolored == 2U);
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].image &&
                s_objects[index].has_image_recolor &&
                s_objects[index].image_recolor == APP_UI_COLOR_WARNING)
        {
            return;
        }
    }
    assert(false);
}

int main(void)
{
    memset(&s_screen, 0, sizeof(s_screen));
    s_screen.live = true;
    _test_dynamic_registry();
    _test_growth_appends_rows();
    _test_empty_registry();
    _test_partial_row_failure();
    _test_all_row_failures();
    _test_normal_image_is_recolored();
    puts("menu app tests passed");
    return 0;
}
