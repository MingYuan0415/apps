/* Prose wrapping guard: LVGL breaks CJK text between any two glyphs, so a
 * closing mark could open a line and an opening mark could end one. The
 * wrapper has to place the breaks itself; these cases assert the two kinsoku
 * rules, the width bound and the fallback contract. Run on host. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_ui.h"

/** @brief Fake object model: only what the prose adapter reads. */
struct lv_obj_t
{
    char text[1024];
    const lv_font_t *font;
    int32_t content_width;
    int32_t letter_space;
};

/** @brief Advance the fake font reports for every glyph. */
#define GLYPH 10
/** @brief Line width in clusters for a maximum width in pixels. */
#define CLUSTERS(pixels) ((pixels) / GLYPH)

static const lv_font_t s_font = { 0 };

/** @brief Length of the UTF-8 cluster starting at @p at. */
static size_t _cluster_bytes(const char *text, size_t at)
{
    const unsigned char lead = (unsigned char)text[at];

    if ((lead & 0x80U) == 0U)
    {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3U;
    }
    return 4U;
}

/** @brief Width of one line in pixels. */
static int32_t _line_width(const char *line, size_t length)
{
    size_t at = 0U;
    int32_t width = 0;

    while (at < length)
    {
        at += _cluster_bytes(line, at);
        width += GLYPH;
    }
    return width;
}

/** @brief First byte of every line in @p text, as offsets into @p text. */
static size_t _line_starts(const char *text, size_t *offsets, size_t capacity)
{
    size_t count = 1U;

    offsets[0] = 0U;
    for (size_t at = 0U; text[at] != '\0'; at++)
    {
        if ((text[at] == '\n') && (count < capacity))
        {
            offsets[count] = at + 1U;
            count++;
        }
    }
    return count;
}

static const char *_line_end(const char *start)
{
    const char *nl = strchr(start, '\n');

    return (nl != NULL) ? nl : (start + strlen(start));
}

/** @brief True when @p code is a mark that may not open a line. */
static bool _opens_a_line_illegally(const char *start)
{
    static const char *const marks[] = { "，", "。", "、", "；", "：", "！",
                                         "？", "）", "】", "」", "…"
                                       };

    for (size_t i = 0U; i < sizeof(marks) / sizeof(marks[0]); i++)
    {
        if (strncmp(start, marks[i], strlen(marks[i])) == 0)
        {
            return true;
        }
    }
    return false;
}

/** @brief True when the line before the newline ends with an opening mark. */
static bool _ends_a_line_illegally(const char *start, const char *end)
{
    static const char *const marks[] = { "（", "【", "「", "《", "〔" };

    for (size_t i = 0U; i < sizeof(marks) / sizeof(marks[0]); i++)
    {
        const size_t length = strlen(marks[i]);

        if ((size_t)(end - start) >= length &&
                strncmp(end - length, marks[i], length) == 0)
        {
            return true;
        }
    }
    return false;
}

/** @brief Assert the two kinsoku rules and the width bound for @p wrapped. */
static int _check_invariants(const char *wrapped, int32_t max_width)
{
    size_t offsets[32];
    const size_t lines = _line_starts(wrapped, offsets, 32U);
    int failures = 0;

    for (size_t i = 0U; i < lines; i++)
    {
        const char *start = wrapped + offsets[i];
        const char *end = _line_end(start);
        const int32_t width = _line_width(start, (size_t)(end - start));

        if ((i > 0U) && _opens_a_line_illegally(start))
        {
            printf("FAIL line %zu opens with a closing mark: %.12s\n", i, start);
            failures++;
        }
        if ((i + 1U < lines) && _ends_a_line_illegally(start, end))
        {
            printf("FAIL line %zu ends with an opening mark\n", i);
            failures++;
        }
        if (width > max_width)
        {
            printf("FAIL line %zu is %d px wide, limit %d\n", i, width,
                   max_width);
            failures++;
        }
    }
    return failures;
}

int32_t lv_font_get_glyph_width(const lv_font_t *font, uint32_t letter,
                                uint32_t letter_next)
{
    (void)font;
    (void)letter;
    (void)letter_next;
    return GLYPH;
}

void lv_obj_update_layout(lv_obj_t *object)
{
    (void)object;
}

int32_t lv_obj_get_content_width(const lv_obj_t *object)
{
    return (object != NULL) ? object->content_width : 0;
}

const lv_font_t *lv_obj_get_style_text_font(const lv_obj_t *object, int selector)
{
    (void)selector;
    return (object != NULL) ? object->font : NULL;
}

int32_t lv_obj_get_style_text_letter_space(const lv_obj_t *object,
        int selector)
{
    (void)selector;
    return (object != NULL) ? object->letter_space : 0;
}

void lv_label_set_text(lv_obj_t *label, const char *text)
{
    (void)snprintf(label->text, sizeof(label->text), "%s", text);
}

const char *lv_label_get_text(const lv_obj_t *label)
{
    return label->text;
}

int main(void)
{
    int failures = 0;
    char wrapped[512];
    size_t used;

    /* The alert advice that shipped with a line-break orphan: 28 clusters at 15
     * per line put the second comma at the start of line two. */
    {
        const char *advice =
            "请做好防暴雨准备，尽量避免外出，注意道路积水与山洪风险。";
        const int32_t max_width = GLYPH * 15;

        used = app_ui_wrap_prose(wrapped, sizeof(wrapped), advice, &s_font, 0,
                                 max_width);
        if (used == 0U)
        {
            printf("FAIL advice did not wrap\n");
            failures++;
        }
        else
        {
            const char *expected =
                "请做好防暴雨准备，尽量避免外\n出，注意道路积水与山洪风险。";

            if (strcmp(wrapped, expected) != 0)
            {
                printf("FAIL advice wrapped as <%s>\n", wrapped);
                failures++;
            }
            failures += _check_invariants(wrapped, max_width);
        }
    }

    /* An opening mark may not be left at the end of a line. */
    {
        const char *text = "预警（随时更新）";
        const int32_t max_width = GLYPH * 3;

        used = app_ui_wrap_prose(wrapped, sizeof(wrapped), text, &s_font, 0,
                                 max_width);
        if ((used == 0U) || (strcmp(wrapped, "预警\n（随时\n更新）") != 0))
        {
            printf("FAIL opening mark wrap <%s>\n", wrapped);
            failures++;
        }
        else
        {
            failures += _check_invariants(wrapped, max_width);
        }
    }

    /* Latin words stay whole while a space takes the break. */
    {
        const char *text = "风速 12 km/h 阵风 20 km/h";
        const int32_t max_width = GLYPH * 7;

        used = app_ui_wrap_prose(wrapped, sizeof(wrapped), text, &s_font, 0,
                                 max_width);
        if (used == 0U)
        {
            printf("FAIL mixed text did not wrap\n");
            failures++;
        }
        else
        {
            if (strstr(wrapped, "k\n") != NULL || strstr(wrapped, "/\n") != NULL)
            {
                printf("FAIL split a Latin word: <%s>\n", wrapped);
                failures++;
            }
            failures += _check_invariants(wrapped, max_width);
        }
    }

    /* Text that already fits keeps its single line, without a trailing break. */
    {
        const char *text = "暂无建议";

        used = app_ui_wrap_prose(wrapped, sizeof(wrapped), text, &s_font, 0,
                                 GLYPH * 10);
        if ((used == 0U) || (strcmp(wrapped, text) != 0))
        {
            printf("FAIL short text changed: <%s>\n", wrapped);
            failures++;
        }
    }

    /* A buffer that cannot hold the wrapped text reports 0 so the caller keeps
     * the original instead of publishing a truncated paragraph. */
    {
        const char *advice =
            "请做好防暴雨准备，尽量避免外出，注意道路积水与山洪风险。";

        used = app_ui_wrap_prose(wrapped, 8U, advice, &s_font, 0, GLYPH * 15);
        if (used != 0U)
        {
            printf("FAIL undersized buffer reported %zu bytes\n", used);
            failures++;
        }
    }

    /* Without a usable width the wrapper stays out of the way. */
    {
        const char *text = "暂无说明";

        if (app_ui_wrap_prose(wrapped, sizeof(wrapped), text, &s_font, 0, 0) != 0U)
        {
            printf("FAIL wrapped without a width\n");
            failures++;
        }
    }

    /* The adapter reads the label's own width, font and spacing: it must leave
     * the line breaks in the label text, and fall back to the raw prose when
     * the label has no resolved width. */
    {
        const char *advice =
            "请做好防暴雨准备，尽量避免外出，注意道路积水与山洪风险。";
        struct lv_obj_t label = { 0 };

        label.font = &s_font;
        label.content_width = GLYPH * 15;
        app_ui_set_prose_text(&label, advice, wrapped, sizeof(wrapped));
        if (strcmp(lv_label_get_text(&label), wrapped) != 0 ||
                strchr(wrapped, '\n') == NULL)
        {
            printf("FAIL adapter did not wrap: <%s>\n", lv_label_get_text(&label));
            failures++;
        }

        memset(&label, 0, sizeof(label));
        label.font = &s_font;
        app_ui_set_prose_text(&label, advice, wrapped, sizeof(wrapped));
        if (strcmp(lv_label_get_text(&label), advice) != 0)
        {
            printf("FAIL adapter changed unresolved text\n");
            failures++;
        }
    }

    if (failures == 0)
    {
        printf("app_ui_text_test: all cases passed\n");
    }
    return failures;
}