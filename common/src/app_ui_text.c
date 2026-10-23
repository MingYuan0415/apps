/** @file CJK-aware prose wrapping for application labels.
 *
 * LVGL breaks CJK text between any two glyphs (`lv_text_is_a_word` in
 * src/misc/lv_text_private.h treats each one as a word), so a closing mark can
 * land at the start of a line, and its break-char path reports "no word" when
 * the mark does not fit, so the mark cannot be hung on the previous line
 * either. Prose therefore gets its line breaks decided here and handed to the
 * label as explicit newlines; the label keeps LV_LABEL_LONG_WRAP so a
 * measurement difference can never clip text.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app_ui.h"

static size_t _cluster_bytes(const char *text, size_t index)
{
    const uint8_t lead = (uint8_t)text[index];
    size_t bytes = 1U;

    if ((lead & 0x80U) == 0U)
    {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        bytes = 2U;
    }
    else if ((lead & 0xF0U) == 0xE0U)
    {
        bytes = 3U;
    }
    else if ((lead & 0xF8U) == 0xF0U)
    {
        bytes = 4U;
    }
    /* A truncated sequence at the end of the string stays a single byte. */
    for (size_t offset = 1U; offset < bytes; offset++)
    {
        if (text[index + offset] == '\0')
        {
            return offset;
        }
    }
    return bytes;
}

static uint32_t _cluster_code(const char *text, size_t index)
{
    const uint8_t *bytes = (const uint8_t *)(text + index);

    switch (_cluster_bytes(text, index))
    {
    case 2U:
        return ((uint32_t)(bytes[0] & 0x1FU) << 6) |
               (uint32_t)(bytes[1] & 0x3FU);
    case 3U:
        return ((uint32_t)(bytes[0] & 0x0FU) << 12) |
               ((uint32_t)(bytes[1] & 0x3FU) << 6) |
               (uint32_t)(bytes[2] & 0x3FU);
    case 4U:
        return ((uint32_t)(bytes[0] & 0x07U) << 18) |
               ((uint32_t)(bytes[1] & 0x3FU) << 12) |
               ((uint32_t)(bytes[2] & 0x3FU) << 6) |
               (uint32_t)(bytes[3] & 0x3FU);
    default:
        return (uint32_t)bytes[0];
    }
}

static size_t _previous_cluster(const char *text, size_t index)
{
    size_t start = index;

    /* Walk back to the lead byte of the cluster that ends at `index`. */
    while (start > 0U)
    {
        start--;
        if (((uint8_t)text[start] & 0xC0U) != 0x80U)
        {
            break;
        }
    }
    return start;
}

/** @brief Cluster classes LVGL itself treats as words, i.e. breakable anywhere.
 *
 * Mirrors the ranges of `lv_text_is_a_word`; keep the two in step.
 */
static bool _is_cjk(uint32_t code)
{
    return ((code >= 0x2E80U) && (code <= 0x2EFFU)) ||
           ((code >= 0x3000U) && (code <= 0x303FU)) ||
           ((code >= 0x3040U) && (code <= 0x30FFU)) ||
           ((code >= 0x31C0U) && (code <= 0x31EFU)) ||
           ((code >= 0x4E00U) && (code <= 0x9FFFU)) ||
           ((code >= 0xFE10U) && (code <= 0xFE1FU)) ||
           ((code >= 0xFF01U) && (code <= 0xFF5EU));
}

/** @brief Marks that may not open a line (行首禁则). */
static bool _forbidden_line_start(uint32_t code)
{
    switch (code)
    {
    case 0x2026U: /* … */
    case 0x3001U: /* 、 */
    case 0x3002U: /* 。 */
    case 0x3009U: /* 〉 */
    case 0x300BU: /* 》 */
    case 0x300DU: /* 」 */
    case 0x300FU: /* 』 */
    case 0x3011U: /* 】 */
    case 0x3015U: /* 〕 */
    case 0xFF01U: /* ！ */
    case 0xFF09U: /* ） */
    case 0xFF0CU: /* ， */
    case 0xFF0EU: /* ． */
    case 0xFF1AU: /* ： */
    case 0xFF1BU: /* ； */
    case 0xFF1FU: /* ？ */
    case 0xFF3DU: /* ］ */
    case 0xFF5DU: /* ｝ */
    case 0xFF65U: /* ･ */
    case (uint32_t)'!':
    case (uint32_t)'%':
    case (uint32_t)')':
    case (uint32_t)',':
    case (uint32_t)'.':
    case (uint32_t)':':
    case (uint32_t)';':
    case (uint32_t)'?':
        return true;
    default:
        return false;
    }
}

/** @brief Marks that may not end a line (行末禁则). */
static bool _forbidden_line_end(uint32_t code)
{
    switch (code)
    {
    case 0x3008U: /* 〈 */
    case 0x300AU: /* 《 */
    case 0x300CU: /* 「 */
    case 0x300EU: /* 『 */
    case 0x3010U: /* 【 */
    case 0x3014U: /* 〔 */
    case 0xFF08U: /* （ */
    case 0xFF3BU: /* ［ */
    case 0xFF5BU: /* ｛ */
    case (uint32_t)'(':
    case (uint32_t)'[':
    case (uint32_t)'{':
        return true;
    default:
        return false;
    }
}

/** @brief True when the text may break before `at`. */
static bool _break_allowed(const char *text, size_t at)
{
    uint32_t previous;

    if (at == 0U)
    {
        return false;
    }
    previous = _cluster_code(text, _previous_cluster(text, at));
    /* Latin words stay whole; CJK clusters and spaces take a break. */
    return (previous == (uint32_t)' ') || _is_cjk(previous);
}

static int32_t _cluster_width(const char *text, size_t index,
                              const lv_font_t *font, int32_t letter_space)
{
    const size_t bytes = _cluster_bytes(text, index);
    const uint32_t code = _cluster_code(text, index);
    const uint32_t next = (text[index + bytes] != '\0')
                          ? _cluster_code(text, index + bytes) : 0U;
    const int32_t width = lv_font_get_glyph_width(font, code, next);

    return (width > 0) ? (width + letter_space) : width;
}

/** @brief End of the longest run that fits, ending on a break LVGL accepts. */
static size_t _fitting_end(const char *text, size_t length, size_t start,
                           const lv_font_t *font, int32_t letter_space,
                           int32_t max_width)
{
    size_t cursor = start;
    size_t fitted = start;
    int32_t used = 0;

    while (cursor < length)
    {
        const size_t bytes = _cluster_bytes(text, cursor);
        const int32_t width = _cluster_width(text, cursor, font, letter_space);

        if ((cursor > start) && ((used + width) > max_width))
        {
            break;
        }
        used += width;
        cursor += bytes;
        if (_break_allowed(text, cursor))
        {
            fitted = cursor;
        }
    }
    if (fitted > start)
    {
        return fitted;
    }
    if (cursor > start)
    {
        /* One unbreakable run (a long Latin word): overflow it rather than
         * emit an empty line. */
        return cursor;
    }
    return start + _cluster_bytes(text, start);
}

/** @brief Apply the two kinsoku rules to a candidate break at `end`. */
static size_t _pull_back(const char *text, size_t start, size_t end,
                         size_t length)
{
    const size_t minimum = start + _cluster_bytes(text, start);

    while ((end < length) && (end > minimum) &&
            _forbidden_line_start(_cluster_code(text, end)))
    {
        end = _previous_cluster(text, end);
    }
    while ((end > minimum) &&
            _forbidden_line_end(_cluster_code(text, _previous_cluster(text,
                    end))))
    {
        end = _previous_cluster(text, end);
    }
    return end;
}

size_t app_ui_wrap_prose(char *out, size_t out_size, const char *text,
                         const lv_font_t *font, int32_t letter_space,
                         int32_t max_width)
{
    size_t written = 0U;
    size_t start = 0U;
    size_t length;

    if ((out == NULL) || (out_size == 0U))
    {
        return 0U;
    }
    out[0] = '\0';
    if ((text == NULL) || (text[0] == '\0') || (font == NULL) ||
            (max_width <= 0))
    {
        return 0U;
    }
    length = strlen(text);
    while (start < length)
    {
        size_t end = _fitting_end(text, length, start, font, letter_space,
                                  max_width);

        end = _pull_back(text, start, end, length);
        if (end <= start)
        {
            end = start + _cluster_bytes(text, start);
        }
        /* Reserve the terminator for the final segment and one extra byte for
         * the break that precedes a following segment. Reserving two bytes on
         * the final segment would reject a buffer that holds the text exactly. */
        const size_t reserve = (end < length) ? 2U : 1U;

        if ((written + (end - start) + reserve) > out_size)
        {
            /* Too small to hold the wrapped text: let the caller keep the
             * original rather than publish a truncated paragraph. */
            out[0] = '\0';
            return 0U;
        }
        memcpy(out + written, text + start, end - start);
        written += end - start;
        out[written] = '\0';
        start = end;
        if (start < length)
        {
            out[written] = '\n';
            written += 1U;
            out[written] = '\0';
        }
    }
    return written;
}

void app_ui_set_prose_text(lv_obj_t *label, const char *text,
                           char *scratch, size_t scratch_size)
{
    int32_t max_width;
    size_t used = 0U;

    if (label == NULL)
    {
        return;
    }
    if ((text != NULL) && (text[0] != '\0') && (scratch != NULL) &&
            (scratch_size > 0U))
    {
        lv_obj_update_layout(label);
        max_width = lv_obj_get_content_width(label);
        used = app_ui_wrap_prose(scratch, scratch_size, text,
                                 lv_obj_get_style_text_font(label,
                                     LV_PART_MAIN),
                                 (int32_t)lv_obj_get_style_text_letter_space(
                                     label, LV_PART_MAIN), max_width);
    }
    lv_label_set_text(label, (used > 0U) ? scratch : ((text != NULL) ? text
            : ""));
}