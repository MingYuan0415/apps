/** @file Minimal LVGL API model used by application host tests. */
#ifndef __APPS_HOST_LVGL_H__
#define __APPS_HOST_LVGL_H__

#include <stdint.h>

/** @brief Opaque fake LVGL object. */
typedef struct lv_obj_t lv_obj_t;
/** @brief Opaque fake LVGL event. */
typedef struct lv_event_t lv_event_t;
/** @brief Fake LVGL font object. */
typedef struct lv_font_t
{
    uint8_t marker;
} lv_font_t;
typedef struct lv_image_dsc_t
{
    uint32_t marker;
} lv_image_dsc_t;

typedef uint32_t lv_color_t;
typedef int32_t lv_event_code_t;
/** @brief Fake LVGL event callback. */
typedef void (*lv_event_cb_t)(lv_event_t *event);

/** @brief Minimal LVGL result values used by draw-buffer declarations. */
typedef enum
{
    LV_RESULT_OK = 0,
    LV_RESULT_INVALID,
} lv_result_t;

/** @brief Minimal fake LVGL draw buffer exposed by recent tasks. */
typedef struct lv_draw_buf
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t color_format;
    uint32_t data_size;
    const void *data;
} lv_draw_buf_t;

#define LV_ALIGN_TOP_LEFT           0
#define LV_DIR_VER                  1
#define LV_EVENT_CLICKED            1
#define LV_EVENT_VALUE_CHANGED      2
#define LV_FLEX_ALIGN_CENTER        1
#define LV_FLEX_ALIGN_SPACE_BETWEEN 2
#define LV_FLEX_ALIGN_START         0
#define LV_FLEX_FLOW_COLUMN         0
#define LV_FLEX_FLOW_ROW            1
#define LV_FONT_DEFAULT             ((const lv_font_t *)0)
#define LV_LABEL_LONG_DOT            0
#define LV_LABEL_LONG_WRAP           1
#define LV_LABEL_LONG_SCROLL_CIRCULAR 2
#define LV_OBJ_FLAG_SCROLLABLE       1
#define LV_OBJ_FLAG_CLICKABLE        2
#define LV_OBJ_FLAG_CLICK_FOCUSABLE  4
#define LV_OBJ_FLAG_GESTURE_BUBBLE   8
#define LV_OBJ_FLAG_SCROLL_CHAIN     16
#define LV_OBJ_FLAG_SCROLL_ELASTIC   32
#define LV_OBJ_FLAG_SCROLL_MOMENTUM  64
#define LV_OBJ_FLAG_HIDDEN            128
#define LV_OBJ_FLAG_PRESS_LOCK        256
#define LV_OPA_COVER                 255
#define LV_OPA_TRANSP                  0
#define LV_PART_MAIN                   0
#define LV_PART_INDICATOR              1
#define LV_PART_KNOB                   2
#define LV_SCROLLBAR_MODE_AUTO       0
#define LV_SIZE_CONTENT              (-1)
#define LV_STATE_DISABLED            2
#define LV_STATE_PRESSED             1
#define LV_STATE_CHECKED             4
#define LV_TEXT_ALIGN_CENTER         0
#define LV_TEXT_ALIGN_RIGHT          1
#define LV_PCT(value)                (value)
#define LV_SYMBOL_LEFT              "left"
#define LV_SYMBOL_RIGHT             "right"
#define LV_SYMBOL_TRASH             "trash"
#define LV_SYMBOL_WIFI              "wifi"
#define LV_SYMBOL_BLUETOOTH         "bluetooth"
#define LV_SYMBOL_BATTERY_FULL      "battery"
#define LV_SYMBOL_GPS               "weather"
#define LV_SYMBOL_BELL              "clock"
#define LV_SYMBOL_AUDIO             "audio"
#define LV_SYMBOL_SETTINGS          "settings"
#define LV_SYMBOL_LIST              "list"

/** @brief Create a fake generic object. */
lv_obj_t *lv_obj_create(lv_obj_t *parent);
/** @brief Create a fake button. */
lv_obj_t *lv_button_create(lv_obj_t *parent);
/** @brief Create a fake label. */
lv_obj_t *lv_label_create(lv_obj_t *parent);
/** @brief Create a fake image. */
lv_obj_t *lv_image_create(lv_obj_t *parent);
/** @brief Create a fake arc. */
lv_obj_t *lv_arc_create(lv_obj_t *parent);
/** @brief Create a fake switch. */
lv_obj_t *lv_switch_create(lv_obj_t *parent);
/** @brief Set fake arc background angles. */
void lv_arc_set_bg_angles(lv_obj_t *object, int32_t start, int32_t end);
/** @brief Set fake arc indicator angles. */
void lv_arc_set_angles(lv_obj_t *object, int32_t start, int32_t end);
/** @brief Set fake arc rotation. */
void lv_arc_set_rotation(lv_obj_t *object, int32_t rotation);
/** @brief Remove a fake style selector. */
void lv_obj_remove_style(lv_obj_t *object, void *style, int selector);
/** @brief Set a fake image source. */
void lv_image_set_src(lv_obj_t *object, const lv_image_dsc_t *source);
/** @brief Return the index-th live fake child, or NULL. */
lv_obj_t *lv_obj_get_child(lv_obj_t *object, int32_t index);
/** @brief Delete a fake object. */
void lv_obj_delete(lv_obj_t *object);
/** @brief Register a fake object event callback. */
void lv_obj_add_event_cb(lv_obj_t *object, lv_event_cb_t callback,
                         lv_event_code_t code, void *user_data);
/** @brief Add a state bit to a fake object. */
void lv_obj_add_state(lv_obj_t *object, uint32_t state);
/** @brief Remove a state bit from a fake object. */
void lv_obj_remove_state(lv_obj_t *object, uint32_t state);
/** @brief Add a behavior flag to a fake object. */
void lv_obj_add_flag(lv_obj_t *object, uint32_t flag);
/** @brief Remove a behavior flag from a fake object. */
void lv_obj_remove_flag(lv_obj_t *object, uint32_t flag);
/** @brief Return a fake event code. */
lv_event_code_t lv_event_get_code(lv_event_t *event);
/** @brief Return fake callback user data. */
void *lv_event_get_user_data(lv_event_t *event);
/** @brief Set fake label text. */
void lv_label_set_text(lv_obj_t *label, const char *text);
/** @brief Record an explicitly assigned fake text font. */
void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font,
                                int selector);

/**
 * @brief Generate no-op LVGL compatibility functions for layout-only tests.
 * @note Generated names intentionally mirror the external LVGL API.
 */
#define APPS_HOST_LV_NOOP_2(name, type1, type2) \
    static inline void name(type1 first, type2 second) \
    { \
        (void)first; \
        (void)second; \
    }

#define APPS_HOST_LV_NOOP_3(name, type1, type2, type3) \
    static inline void name(type1 first, type2 second, type3 third) \
    { \
        (void)first; \
        (void)second; \
        (void)third; \
    }

#define APPS_HOST_LV_NOOP_4(name, type1, type2, type3, type4) \
    static inline void name(type1 first, type2 second, type3 third, type4 fourth) \
    { \
        (void)first; \
        (void)second; \
        (void)third; \
        (void)fourth; \
    }

APPS_HOST_LV_NOOP_2(lv_obj_set_flex_flow, lv_obj_t *, int)
APPS_HOST_LV_NOOP_2(lv_obj_set_flex_grow, lv_obj_t *, int)
APPS_HOST_LV_NOOP_2(lv_obj_set_height, lv_obj_t *, int32_t)
APPS_HOST_LV_NOOP_2(lv_obj_set_scroll_dir, lv_obj_t *, int)
APPS_HOST_LV_NOOP_2(lv_obj_set_scrollbar_mode, lv_obj_t *, int)
APPS_HOST_LV_NOOP_2(lv_obj_set_width, lv_obj_t *, int32_t)
APPS_HOST_LV_NOOP_2(lv_label_set_long_mode, lv_obj_t *, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_size, lv_obj_t *, int32_t, int32_t)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_bg_color, lv_obj_t *, lv_color_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_bg_opa, lv_obj_t *, int, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_arc_color, lv_obj_t *, lv_color_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_arc_opa, lv_obj_t *, int, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_arc_width, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_all, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_bottom, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_column, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_gap, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_left, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_right, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_row, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_pad_top, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_radius, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_shadow_width, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_text_align, lv_obj_t *, int, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_text_color, lv_obj_t *, lv_color_t, int)
APPS_HOST_LV_NOOP_3(lv_obj_set_style_text_line_space, lv_obj_t *, int32_t, int)
APPS_HOST_LV_NOOP_4(lv_obj_align, lv_obj_t *, int, int32_t, int32_t)
APPS_HOST_LV_NOOP_4(lv_obj_set_flex_align, lv_obj_t *, int, int, int)

/** @brief Convert a host RGB value to the fake LVGL color type. */
static inline lv_color_t lv_color_hex(uint32_t color)
{
    return color;
}

/** @brief Remove fake object styles. */
static inline void lv_obj_remove_style_all(lv_obj_t *object)
{
    (void)object;
}

/** @brief Center a fake object. */
static inline void lv_obj_center(lv_obj_t *object)
{
    (void)object;
}

#undef APPS_HOST_LV_NOOP_2
#undef APPS_HOST_LV_NOOP_3
#undef APPS_HOST_LV_NOOP_4

#endif /* __APPS_HOST_LVGL_H__ */
