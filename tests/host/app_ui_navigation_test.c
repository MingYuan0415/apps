#include "app_ui.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_QUEUE_CAPACITY 24U
#define TEST_OBJECT_CAPACITY 32U

struct lv_obj_t
{
    bool live;
    bool label;
    bool explicit_font;
    lv_obj_t *parent;
    const lv_font_t *font;
    char text[32];
};

struct lv_event_t
{
    lv_event_code_t code;
};

typedef struct test_command
{
    app_manager_nav_request_t request;
    char app_id[APP_MANAGER_ID_BYTES];
    char page_id[APP_MANAGER_ID_BYTES];
    app_manager_nav_completion_cb_t completion;
    void *completion_context;
} test_command_t;

static test_command_t s_commands[TEST_QUEUE_CAPACITY];
static size_t s_command_count;
static bool s_reject_next;
static app_manager_nav_operation_t s_last_operation;
static char s_last_app_id[APP_MANAGER_ID_BYTES];
static char s_last_page_id[APP_MANAGER_ID_BYTES];
static unsigned s_execute_count;
static unsigned s_completion_count;
static esp_err_t s_completion_result;
static lv_obj_t s_screen;
static lv_obj_t s_objects[TEST_OBJECT_CAPACITY];

static lv_obj_t *_test_object_create(lv_obj_t *parent, bool label)
{
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (!s_objects[index].live)
        {
            s_objects[index] = (lv_obj_t)
            {
                .live = true,
                .label = label,
                .parent = parent,
            };
            return &s_objects[index];
        }
    }
    assert(false);
    return NULL;
}

static size_t _test_explicit_font_count(const char *text,
                                        const lv_font_t *font)
{
    size_t count = 0U;
    for (size_t index = 0U; index < TEST_OBJECT_CAPACITY; ++index)
    {
        if (s_objects[index].live && s_objects[index].label &&
                s_objects[index].explicit_font &&
                s_objects[index].font == font &&
                strcmp(s_objects[index].text, text) == 0)
        {
            ++count;
        }
    }
    return count;
}

lv_obj_t *lv_obj_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false);
}

lv_obj_t *lv_button_create(lv_obj_t *parent)
{
    return _test_object_create(parent, false);
}

lv_obj_t *lv_label_create(lv_obj_t *parent)
{
    return _test_object_create(parent, true);
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
    (void)object;
    (void)callback;
    (void)code;
    (void)user_data;
}

lv_event_code_t lv_event_get_code(lv_event_t *event)
{
    return event->code;
}

void lv_label_set_text(lv_obj_t *label, const char *text)
{
    assert(label != NULL);
    (void)snprintf(label->text, sizeof(label->text), "%s", text);
}

void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font,
                                int selector)
{
    (void)selector;
    assert(object != NULL);
    object->font = font;
    object->explicit_font = true;
}

lv_obj_t *app_manager_this_page_screen(void)
{
    return &s_screen;
}

static void _test_reset_queue(void)
{
    memset(s_commands, 0, sizeof(s_commands));
    s_command_count = 0;
    s_reject_next = false;
    s_last_operation = APP_MANAGER_NAV_OP_RUN;
    memset(s_last_app_id, 0, sizeof(s_last_app_id));
    memset(s_last_page_id, 0, sizeof(s_last_page_id));
    s_execute_count = 0;
    s_completion_count = 0;
    s_completion_result = ESP_OK;
}

static void _test_drain_queue(void)
{
    size_t index = 0;
    while (index < s_command_count)
    {
        test_command_t command = s_commands[index++];
        s_last_operation = command.request.operation;
        (void)snprintf(s_last_app_id, sizeof(s_last_app_id), "%s",
                       command.request.app_id != NULL ?
                       command.request.app_id : "");
        (void)snprintf(s_last_page_id, sizeof(s_last_page_id), "%s",
                       command.request.page_id != NULL ?
                       command.request.page_id : "");
        ++s_execute_count;
        assert(command.completion != NULL);
        command.completion(s_completion_result,
                           command.completion_context);
        ++s_completion_count;
    }
    s_command_count = 0;
}

static void _test_expect_run(const char *expected_id)
{
    assert(s_last_operation == APP_MANAGER_NAV_OP_RUN);
    assert(strcmp(s_last_app_id, expected_id) == 0);
    assert(s_last_page_id[0] == '\0');
    assert(s_execute_count > 0U);
}

esp_err_t app_manager_navigate_async(
    const app_manager_nav_request_t *request,
    app_manager_nav_completion_cb_t completion, void *context)
{
    assert(request != NULL);
    assert(!request->has_arguments);
    assert(request->transition.effect == APP_MANAGER_TRANSITION_DEFAULT);
    assert(request->transition.duration_ms == 0U);
    assert(request->transition.reserved == 0U);
    if (s_reject_next)
    {
        s_reject_next = false;
        return ESP_ERR_NO_MEM;
    }
    if (s_command_count == TEST_QUEUE_CAPACITY)
    {
        return ESP_ERR_NO_MEM;
    }
    test_command_t *command = &s_commands[s_command_count++];
    memset(command, 0, sizeof(*command));
    command->request = *request;
    command->completion = completion;
    command->completion_context = context;
    if (request->app_id != NULL)
    {
        (void)snprintf(command->app_id, sizeof(command->app_id), "%s",
                       request->app_id);
        command->request.app_id = command->app_id;
    }
    if (request->page_id != NULL)
    {
        (void)snprintf(command->page_id, sizeof(command->page_id), "%s",
                       request->page_id);
        command->request.page_id = command->page_id;
    }
    return ESP_OK;
}

const lv_font_t *app_manager_get_font(app_theme_font_id_t id)
{
    (void)id;
    return LV_FONT_DEFAULT;
}

int main(void)
{
    memset(&s_screen, 0, sizeof(s_screen));
    s_screen.live = true;
    memset(s_objects, 0, sizeof(s_objects));
    app_ui_page_t page;
    app_ui_page_create(&page, "符号字体", true);
    (void)app_ui_add_action(page.content, NULL, "导航", NULL, NULL, NULL);
    assert(_test_explicit_font_count(LV_SYMBOL_LEFT, LV_FONT_DEFAULT) == 1U);
    assert(_test_explicit_font_count(LV_SYMBOL_RIGHT, LV_FONT_DEFAULT) == 2U);
    app_ui_page_destroy(&page);

    _test_reset_queue();
    char dynamic_id[APP_MANAGER_ID_BYTES] = "temporary-dynamic-app";
    app_ui_request_run(dynamic_id);
    assert(s_command_count == 1U);
    (void)snprintf(dynamic_id, sizeof(dynamic_id), "%s", "mutated-app-id");
    _test_drain_queue();
    assert(s_execute_count == 1U);
    assert(s_completion_count == 1U);
    _test_expect_run("temporary-dynamic-app");

    _test_reset_queue();
    for (unsigned index = 0; index < TEST_QUEUE_CAPACITY; ++index)
    {
        char id[APP_MANAGER_ID_BYTES];
        (void)snprintf(id, sizeof(id), "queued-%u", index);
        app_ui_request_run(id);
    }
    assert(s_command_count == TEST_QUEUE_CAPACITY);
    app_ui_request_run("overflow");
    assert(s_command_count == TEST_QUEUE_CAPACITY);
    _test_drain_queue();
    assert(s_execute_count == TEST_QUEUE_CAPACITY);
    assert(s_completion_count == TEST_QUEUE_CAPACITY);

    _test_reset_queue();
    s_reject_next = true;
    app_ui_request_run("rejected");
    assert(s_command_count == 0U);
    app_ui_request_run("retry");
    assert(s_command_count == 1U);
    _test_drain_queue();
    assert(s_execute_count == 1U);
    assert(s_completion_count == 1U);
    _test_expect_run("retry");

    _test_reset_queue();
    app_ui_request_back();
    assert(s_command_count == 1U);
    _test_drain_queue();
    assert(s_last_operation == APP_MANAGER_NAV_OP_BACK);
    assert(s_last_app_id[0] == '\0');
    assert(s_last_page_id[0] == '\0');
    assert(s_completion_count == 1U);

    _test_reset_queue();
    char page_app[APP_MANAGER_ID_BYTES] = "settings";
    char page_id[APP_MANAGER_ID_BYTES] = "power";
    app_ui_request_open_page(page_app, page_id);
    assert(s_command_count == 1U);
    (void)snprintf(page_app, sizeof(page_app), "%s", "mutated-app");
    (void)snprintf(page_id, sizeof(page_id), "%s", "mutated-page");
    _test_drain_queue();
    assert(s_last_operation == APP_MANAGER_NAV_OP_OPEN_PAGE);
    assert(strcmp(s_last_app_id, "settings") == 0);
    assert(strcmp(s_last_page_id, "power") == 0);
    assert(s_completion_count == 1U);

    _test_reset_queue();
    s_completion_result = ESP_FAIL;
    app_ui_request_run("completion-failure");
    _test_drain_queue();
    assert(s_completion_count == 1U);

    _test_reset_queue();
    app_ui_request_run("");
    app_ui_request_run(NULL);
    char too_long[APP_MANAGER_ID_BYTES + 1U];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';
    app_ui_request_run(too_long);
    app_ui_request_open_page("settings", "");
    app_ui_request_open_page(NULL, "power");
    assert(s_command_count == 0U);

    puts("app_ui navigation queue tests passed");
    return 0;
}
