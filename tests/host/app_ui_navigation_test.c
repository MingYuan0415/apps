#include "app_ui.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TEST_QUEUE_CAPACITY 24U
#define TEST_APP_ID_BYTES   32U

typedef struct test_command
{
    void (*callback)(void *);
    void *arg;
} test_command_t;

static test_command_t s_commands[TEST_QUEUE_CAPACITY];
static size_t s_command_count;
static bool s_reject_next;
static char s_last_run_id[TEST_APP_ID_BYTES];
static unsigned s_run_count;

static void _test_reset_queue(void)
{
    memset(s_commands, 0, sizeof(s_commands));
    s_command_count = 0;
    s_reject_next = false;
    memset(s_last_run_id, 0, sizeof(s_last_run_id));
    s_run_count = 0;
}

static void _test_drain_queue(void)
{
    size_t index = 0;
    while (index < s_command_count)
    {
        test_command_t command = s_commands[index++];
        command.callback(command.arg);
    }
    s_command_count = 0;
}

static void _test_expect_run(const char *expected_id)
{
    assert(strcmp(s_last_run_id, expected_id) == 0);
    assert(s_run_count > 0U);
}

esp_err_t app_manager_ui_post(void (*callback)(void *), void *arg)
{
    if (s_reject_next)
    {
        s_reject_next = false;
        return ESP_ERR_NO_MEM;
    }
    if (s_command_count == TEST_QUEUE_CAPACITY)
    {
        return ESP_ERR_NO_MEM;
    }
    s_commands[s_command_count++] = (test_command_t)
    {
        .callback = callback,
        .arg = arg,
    };
    return ESP_OK;
}

esp_err_t app_manager_run(const char *app_id)
{
    (void)snprintf(s_last_run_id, sizeof(s_last_run_id), "%s", app_id);
    ++s_run_count;
    return ESP_OK;
}

esp_err_t app_manager_goback(void)
{
    return ESP_OK;
}

const lv_font_t *app_manager_get_font(app_theme_font_id_t id)
{
    (void)id;
    return LV_FONT_DEFAULT;
}

int main(void)
{
    _test_reset_queue();
    char dynamic_id[TEST_APP_ID_BYTES] = "temporary-dynamic-app";
    app_ui_request_run(dynamic_id);
    assert(s_command_count == 1U);
    (void)snprintf(dynamic_id, sizeof(dynamic_id), "%s", "mutated-app-id");
    _test_drain_queue();
    assert(s_run_count == 1U);
    _test_expect_run("temporary-dynamic-app");

    _test_reset_queue();
    for (unsigned index = 0; index < TEST_QUEUE_CAPACITY; ++index)
    {
        char id[TEST_APP_ID_BYTES];
        (void)snprintf(id, sizeof(id), "queued-%u", index);
        app_ui_request_run(id);
    }
    assert(s_command_count == TEST_QUEUE_CAPACITY);
    app_ui_request_run("overflow");
    assert(s_command_count == TEST_QUEUE_CAPACITY);
    _test_drain_queue();

    _test_reset_queue();
    s_reject_next = true;
    app_ui_request_run("rejected");
    assert(s_command_count == 0U);
    app_ui_request_run("retry");
    assert(s_command_count == 1U);
    _test_drain_queue();
    assert(s_run_count == 1U);
    _test_expect_run("retry");

    _test_reset_queue();
    app_ui_request_run("");
    app_ui_request_run(NULL);
    char too_long[TEST_APP_ID_BYTES + 1U];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';
    app_ui_request_run(too_long);
    assert(s_command_count == 0U);

    puts("app_ui navigation queue tests passed");
    return 0;
}
