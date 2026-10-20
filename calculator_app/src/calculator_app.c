#define DBG_TAG "calculator_app"
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "calculator_math.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Calculator stays on the shared Emissive palette: canvas and surfaces come
 * from the page skeleton and app_ui_theme, never a local colour literal. */
#define CALCULATOR_COLOR_TEXT       APP_UI_COLOR_TEXT
#define CALCULATOR_COLOR_MUTED      APP_UI_COLOR_MUTED
#define CALCULATOR_COLOR_SURFACE    APP_UI_COLOR_SURFACE
#define CALCULATOR_COLOR_SURFACE_HI APP_UI_COLOR_SURFACE_HI
#define CALCULATOR_COLOR_ACCENT     APP_UI_COLOR_RAIN
#define CALCULATOR_COLOR_WARNING    APP_UI_COLOR_WARNING

/* Display block plus a five-row keypad fills the 384 px content area, so the
 * page is pinned and only the keys are interactive. */
#define CALCULATOR_DISPLAY_HEIGHT 88
#define CALCULATOR_KEY_RADIUS     LV_RADIUS_CIRCLE
#define CALCULATOR_KEY_GAP        8

static lv_style_t s_key_base;
static lv_style_t s_key_ring;
static lv_style_t s_key_ring_pressed;
static lv_style_t s_key_operator;
static lv_style_t s_key_function;
static lv_style_t s_key_pressed;
static lv_style_t s_display;
static lv_style_t s_keypad;
static lv_style_t s_key_row;
static lv_style_t s_label_right;
static bool s_styles_ready;

static void _calculator_styles_init(void)
{
    if (s_styles_ready)
    {
        return;
    }
    lv_style_init(&s_key_base);
    lv_style_set_radius(&s_key_base, CALCULATOR_KEY_RADIUS);
    lv_style_set_shadow_width(&s_key_base, 0);
    lv_style_set_pad_all(&s_key_base, 0);
    lv_style_set_bg_color(&s_key_base, lv_color_hex(CALCULATOR_COLOR_SURFACE));
    lv_style_set_bg_opa(&s_key_base, LV_OPA_COVER);

    /* Key weight follows the pill-button scale: the one primary action is an
     * accent fill, operators an accent tint, functions the L2 surface. */
    lv_style_init(&s_key_ring);
    lv_style_set_bg_color(&s_key_ring, lv_color_hex(CALCULATOR_COLOR_ACCENT));

    lv_style_init(&s_key_ring_pressed);
    lv_style_set_bg_color(&s_key_ring_pressed,
                          lv_color_hex(APP_UI_COLOR_RAIN_HI));

    lv_style_init(&s_key_operator);
    lv_style_set_bg_color(&s_key_operator,
                          lv_color_hex(APP_UI_COLOR_RAIN_TINT));

    lv_style_init(&s_key_function);
    lv_style_set_bg_color(&s_key_function,
                          lv_color_hex(CALCULATOR_COLOR_SURFACE_HI));

    lv_style_init(&s_key_pressed);
    lv_style_set_bg_color(&s_key_pressed,
                          lv_color_hex(APP_UI_COLOR_TRACK));

    lv_style_init(&s_display);
    lv_style_set_pad_row(&s_display, 2);

    lv_style_init(&s_keypad);
    lv_style_set_pad_row(&s_keypad, CALCULATOR_KEY_GAP);

    lv_style_init(&s_key_row);
    lv_style_set_pad_column(&s_key_row, CALCULATOR_KEY_GAP);

    lv_style_init(&s_label_right);
    lv_style_set_text_align(&s_label_right, LV_TEXT_ALIGN_RIGHT);

    s_styles_ready = true;
}

/** @brief Action one keypad button performs on the engine. */
typedef enum
{
    CALCULATOR_KEY_DIGIT = 0,  /**< Append @c digit to the entry. */
    CALCULATOR_KEY_DECIMAL,    /**< Start or extend a fractional entry. */
    CALCULATOR_KEY_SIGN,       /**< Toggle the entry sign. */
    CALCULATOR_KEY_PERCENT,    /**< Divide the entry by one hundred. */
    CALCULATOR_KEY_BACKSPACE,  /**< Delete the last entry character. */
    CALCULATOR_KEY_CLEAR,      /**< Reset the whole calculation. */
    CALCULATOR_KEY_OPERATOR,   /**< Arm the binary operation @c op. */
    CALCULATOR_KEY_EQUALS,     /**< Finish the pending operation. */
} calculator_key_kind_t;

/** @brief One immutable keypad button: caption, action and colour role. */
typedef struct calculator_key
{
    const char *caption;         /**< Key text or LVGL symbol. */
    calculator_key_kind_t kind;  /**< Engine action performed on click. */
    calculator_math_op_t op;     /**< Operation armed by an OPERATOR key. */
    char digit;                  /**< Digit appended by a DIGIT key. */
    uint32_t text_color;         /**< Caption colour. */
    bool symbol;                 /**< Caption needs the built-in symbol font. */
    bool ring;                   /**< Key is the accent-filled primary action. */
} calculator_key_t;

typedef struct calculator_page_state
{
    app_ui_page_t page;
    calculator_math_t math;
    lv_obj_t *expression_label;
    lv_obj_t *value_label;
} calculator_page_state_t;

_Static_assert(sizeof(calculator_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Calculator page state exceeds lifecycle arena slot");

/* Keys read left to right, four per row, five rows: functions and the pending
 * operand stay above the digits and the primary action sits bottom right. */
#define CALCULATOR_KEY_COLUMNS 4

static const calculator_key_t s_keys[] =
{
    {
        .caption = "C", .kind = CALCULATOR_KEY_CLEAR,
        .text_color = CALCULATOR_COLOR_MUTED,
    },
    {
        .caption = LV_SYMBOL_BACKSPACE, .kind = CALCULATOR_KEY_BACKSPACE,
        .text_color = CALCULATOR_COLOR_MUTED, .symbol = true,
    },
    {
        .caption = "%", .kind = CALCULATOR_KEY_PERCENT,
        .text_color = CALCULATOR_COLOR_MUTED,
    },
    {
        .caption = "÷", .kind = CALCULATOR_KEY_OPERATOR,
        .op = CALCULATOR_MATH_OP_DIVIDE,
        .text_color = CALCULATOR_COLOR_ACCENT,
    },
    {
        .caption = "7", .kind = CALCULATOR_KEY_DIGIT, .digit = '7',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "8", .kind = CALCULATOR_KEY_DIGIT, .digit = '8',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "9", .kind = CALCULATOR_KEY_DIGIT, .digit = '9',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "×", .kind = CALCULATOR_KEY_OPERATOR,
        .op = CALCULATOR_MATH_OP_MULTIPLY,
        .text_color = CALCULATOR_COLOR_ACCENT,
    },
    {
        .caption = "4", .kind = CALCULATOR_KEY_DIGIT, .digit = '4',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "5", .kind = CALCULATOR_KEY_DIGIT, .digit = '5',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "6", .kind = CALCULATOR_KEY_DIGIT, .digit = '6',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "-", .kind = CALCULATOR_KEY_OPERATOR,
        .op = CALCULATOR_MATH_OP_SUBTRACT,
        .text_color = CALCULATOR_COLOR_ACCENT,
    },
    {
        .caption = "1", .kind = CALCULATOR_KEY_DIGIT, .digit = '1',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "2", .kind = CALCULATOR_KEY_DIGIT, .digit = '2',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "3", .kind = CALCULATOR_KEY_DIGIT, .digit = '3',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "+", .kind = CALCULATOR_KEY_OPERATOR,
        .op = CALCULATOR_MATH_OP_ADD,
        .text_color = CALCULATOR_COLOR_ACCENT,
    },
    {
        .caption = "±", .kind = CALCULATOR_KEY_SIGN,
        .text_color = CALCULATOR_COLOR_MUTED,
    },
    {
        .caption = "0", .kind = CALCULATOR_KEY_DIGIT, .digit = '0',
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = ".", .kind = CALCULATOR_KEY_DECIMAL,
        .text_color = CALCULATOR_COLOR_TEXT,
    },
    {
        .caption = "=", .kind = CALCULATOR_KEY_EQUALS,
        .text_color = CALCULATOR_COLOR_ACCENT, .ring = true,
    },
};

#define CALCULATOR_KEY_COUNT (sizeof(s_keys) / sizeof(s_keys[0]))

_Static_assert(CALCULATOR_KEY_COUNT % CALCULATOR_KEY_COLUMNS == 0U,
               "Calculator keypad must hold whole rows");

static void _calculator_render(calculator_page_state_t *state)
{
    const uint32_t color = calculator_math_is_error(&state->math) ?
                           CALCULATOR_COLOR_WARNING : CALCULATOR_COLOR_TEXT;
    if (state->value_label != NULL)
    {
        lv_obj_set_style_text_color(state->value_label, lv_color_hex(color), 0);
        app_ui_label_set_text_if(state->value_label,
                                 calculator_math_display(&state->math));
    }
    if (state->expression_label != NULL)
    {
        app_ui_label_set_text_if(state->expression_label,
                                 calculator_math_expression(&state->math));
    }
}

static void _calculator_key_event(lv_event_t *event)
{
    calculator_page_state_t *state = lv_event_get_user_data(event);
    lv_obj_t *key = lv_event_get_target(event);
    if (state == NULL || key == NULL)
    {
        return;
    }
    const size_t index = (size_t)(uintptr_t)lv_obj_get_user_data(key);
    if (index >= CALCULATOR_KEY_COUNT)
    {
        return;
    }

    const calculator_key_t *descriptor = &s_keys[index];
    switch (descriptor->kind)
    {
    case CALCULATOR_KEY_DIGIT:
        calculator_math_digit(&state->math, descriptor->digit);
        break;
    case CALCULATOR_KEY_DECIMAL:
        calculator_math_decimal(&state->math);
        break;
    case CALCULATOR_KEY_SIGN:
        calculator_math_sign(&state->math);
        break;
    case CALCULATOR_KEY_PERCENT:
        calculator_math_percent(&state->math);
        break;
    case CALCULATOR_KEY_BACKSPACE:
        calculator_math_backspace(&state->math);
        break;
    case CALCULATOR_KEY_CLEAR:
        calculator_math_clear(&state->math);
        break;
    case CALCULATOR_KEY_OPERATOR:
        calculator_math_operator(&state->math, descriptor->op);
        break;
    case CALCULATOR_KEY_EQUALS:
        calculator_math_equals(&state->math);
        break;
    default:
        return;
    }
    _calculator_render(state);
}

static lv_obj_t *_calculator_make_key(lv_obj_t *row,
                                      const calculator_key_t *descriptor,
                                      size_t index, void *user_data)
{
    lv_obj_t *key = lv_button_create(row);
    if (key == NULL)
    {
        return NULL;
    }
    app_ui_click_only(key);
    lv_obj_set_width(key, 0);
    lv_obj_set_flex_grow(key, 1);
    lv_obj_set_height(key, LV_PCT(100));
    lv_obj_add_style(key, &s_key_base, 0);
    lv_obj_add_style(key, &s_key_pressed, LV_STATE_PRESSED);
    if (descriptor->ring)
    {
        lv_obj_add_style(key, &s_key_ring, 0);
        lv_obj_add_style(key, &s_key_ring_pressed, LV_STATE_PRESSED);
    }
    else if (descriptor->kind == CALCULATOR_KEY_OPERATOR)
    {
        lv_obj_add_style(key, &s_key_operator, 0);
    }
    else if (descriptor->kind != CALCULATOR_KEY_DIGIT &&
             descriptor->kind != CALCULATOR_KEY_DECIMAL)
    {
        lv_obj_add_style(key, &s_key_function, 0);
    }
    lv_obj_set_user_data(key, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(key, _calculator_key_event, LV_EVENT_CLICKED,
                        user_data);

    lv_obj_t *caption = lv_label_create(key);
    if (caption != NULL)
    {
        /* LVGL symbol glyphs only exist in the built-in default font. */
        lv_obj_set_style_text_font(caption, descriptor->symbol ? LV_FONT_DEFAULT :
                                   app_ui_font(APP_THEME_FONT_HEAD), 0);
        lv_obj_set_style_text_color(caption, lv_color_hex(
                                        descriptor->ring ?
                                        APP_UI_COLOR_ON_ACCENT :
                                        descriptor->text_color), 0);
        lv_label_set_text(caption, descriptor->caption);
        lv_obj_center(caption);
    }
    return key;
}

static void _calculator_build_display(calculator_page_state_t *state)
{
    lv_obj_t *display = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(display);
    lv_obj_set_width(display, LV_PCT(100));
    lv_obj_set_height(display, CALCULATOR_DISPLAY_HEIGHT);
    lv_obj_set_flex_flow(display, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(display, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    lv_obj_add_style(display, &s_display, 0);
    app_ui_make_passive(display, false);

    state->expression_label = lv_label_create(display);
    if (state->expression_label != NULL)
    {
        lv_obj_set_width(state->expression_label, LV_PCT(100));
        lv_label_set_long_mode(state->expression_label, LV_LABEL_LONG_DOT);
        lv_obj_add_style(state->expression_label, &s_label_right, 0);
        lv_obj_set_style_text_font(state->expression_label,
                                   app_ui_font(APP_THEME_FONT_SMALL), 0);
        lv_obj_set_style_text_color(state->expression_label,
                                    lv_color_hex(CALCULATOR_COLOR_MUTED), 0);
        lv_label_set_text(state->expression_label, "");
    }

    state->value_label = lv_label_create(display);
    if (state->value_label != NULL)
    {
        lv_obj_set_width(state->value_label, LV_PCT(100));
        lv_label_set_long_mode(state->value_label, LV_LABEL_LONG_DOT);
        lv_obj_add_style(state->value_label, &s_label_right, 0);
        lv_obj_set_style_text_font(state->value_label,
                                   app_ui_font(APP_THEME_FONT_BIGL), 0);
        lv_obj_set_style_text_color(state->value_label,
                                    lv_color_hex(CALCULATOR_COLOR_TEXT), 0);
        lv_label_set_text(state->value_label,
                          calculator_math_display(&state->math));
    }
}

static void _calculator_build_keypad(calculator_page_state_t *state)
{
    lv_obj_t *keypad = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(keypad);
    lv_obj_set_width(keypad, LV_PCT(100));
    lv_obj_set_height(keypad, 0);
    lv_obj_set_flex_grow(keypad, 1);
    lv_obj_set_flex_flow(keypad, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(keypad, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_style(keypad, &s_keypad, 0);
    app_ui_make_passive(keypad, false);

    const size_t rows = CALCULATOR_KEY_COUNT / CALCULATOR_KEY_COLUMNS;
    for (size_t row = 0U; row < rows; row++)
    {
        lv_obj_t *line = lv_obj_create(keypad);
        if (line == NULL)
        {
            return;
        }
        lv_obj_remove_style_all(line);
        lv_obj_set_width(line, LV_PCT(100));
        lv_obj_set_height(line, 0);
        lv_obj_set_flex_grow(line, 1);
        lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_style(line, &s_key_row, 0);
        app_ui_make_passive(line, false);

        for (size_t column = 0U; column < CALCULATOR_KEY_COLUMNS; column++)
        {
            const size_t index = row * CALCULATOR_KEY_COLUMNS + column;
            (void)_calculator_make_key(line, &s_keys[index], index, state);
        }
    }
}

static void _calculator_mount(const app_manager_page_context_t *context)
{
    calculator_page_state_t *state = context->state;
    /* A remount reuses the arena, so every render cache and the calculation
     * itself restart from the initial state. */
    memset(state, 0, sizeof(*state));
    calculator_math_init(&state->math);
    _calculator_styles_init();
    app_ui_page_create(&state->page, "计算器", true);
    lv_obj_set_scrollable(state->page.content, false);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
    _calculator_build_display(state);
    _calculator_build_keypad(state);
    _calculator_render(state);
}

static void _calculator_unmount(const app_manager_page_context_t *context)
{
    calculator_page_state_t *state = context->state;
    app_ui_page_destroy(&state->page);
    state->expression_label = NULL;
    state->value_label = NULL;
}

static const app_manager_page_ops_t s_calculator_ops =
{
    .mount = _calculator_mount,
    .unmount = _calculator_unmount,
};

static const app_manager_page_definition_t s_calculator_definition =
{
    .ops = &s_calculator_ops,
    .memory_size = sizeof(calculator_page_state_t),
};

static const app_manager_page_route_t s_calculator_routes[] =
{
    {
        .page_id = "root", .definition = &s_calculator_definition,
        .user_data = NULL
    },
};

APP_MANAGER_APP_EXPORT_META(calculator, APP_IMAGE_CALCULATOR_ICON, "计算器",
                            APP_MANAGER_ID_CALCULATOR, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_calculator_routes, 15U,
                            "四则运算");