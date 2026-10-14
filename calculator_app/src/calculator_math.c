/**
 * @brief  Hardware-independent calculator engine.
 * @note   Compiled on the host by layers/apps/tests/host/calculator_math_test.c.
 */

#include "calculator_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both error lines are display copy owned by the engine: the page renders the
 * failure text instead of re-deriving it. */
#define CALCULATOR_ERROR_TEXT      "错误"
#define CALCULATOR_DIVIDE_ZERO     "除数不能为零"
#define CALCULATOR_RANGE_TEXT      "结果超出范围"

static void _calculator_copy(char *destination, size_t size, const char *text)
{
    (void)snprintf(destination, size, "%s", text != NULL ? text : "");
}

/* The display mirrors the edited entry until an operation commits a result. */
static void _calculator_sync_display(calculator_math_t *math)
{
    _calculator_copy(math->display, sizeof(math->display), math->entry);
}

static size_t _calculator_digit_count(const char *entry)
{
    size_t count = 0U;
    for (const char *scan = entry; scan != NULL && *scan != '\0'; scan++)
    {
        if (*scan >= '0' && *scan <= '9')
        {
            ++count;
        }
    }
    return count;
}

/* Failure is terminal until the user clears or starts a new number. */
static void _calculator_fail(calculator_math_t *math, const char *reason)
{
    _calculator_copy(math->entry, sizeof(math->entry), "0");
    _calculator_copy(math->display, sizeof(math->display),
                     CALCULATOR_ERROR_TEXT);
    _calculator_copy(math->expression, sizeof(math->expression), reason);
    math->accumulator = 0.0;
    math->pending = CALCULATOR_MATH_OP_NONE;
    math->entry_active = false;
    math->error = true;
}

static bool _calculator_evaluate(double lhs, double rhs,
                                 calculator_math_op_t op, double *result,
                                 const char **reason)
{
    double value = 0.0;

    switch (op)
    {
    case CALCULATOR_MATH_OP_ADD:
        value = lhs + rhs;
        break;
    case CALCULATOR_MATH_OP_SUBTRACT:
        value = lhs - rhs;
        break;
    case CALCULATOR_MATH_OP_MULTIPLY:
        value = lhs * rhs;
        break;
    case CALCULATOR_MATH_OP_DIVIDE:
        if (rhs == 0.0)
        {
            *reason = CALCULATOR_DIVIDE_ZERO;
            return false;
        }
        value = lhs / rhs;
        break;
    default:
        return false;
    }

    if (!isfinite(value))
    {
        *reason = CALCULATOR_RANGE_TEXT;
        return false;
    }
    *result = value;
    return true;
}

/* Promote the rendered result into an editable entry so sign and percent act
 * on what the user sees; formatted exponent notation cannot be edited. */
static bool _calculator_adopt_display(calculator_math_t *math)
{
    if (math->entry_active)
    {
        return true;
    }
    if (strchr(math->display, 'e') != NULL ||
            strchr(math->display, 'E') != NULL ||
            strlen(math->display) >= sizeof(math->entry))
    {
        return false;
    }
    _calculator_copy(math->entry, sizeof(math->entry), math->display);
    math->entry_active = true;
    return true;
}

void calculator_math_format(double value, char *text, size_t size)
{
    if (text == NULL || size == 0U)
    {
        return;
    }
    if (value == 0.0)
    {
        _calculator_copy(text, size, "0");
        return;
    }
    if (!isfinite(value))
    {
        _calculator_copy(text, size, CALCULATOR_ERROR_TEXT);
        return;
    }

    /* Trade precision for width instead of truncating a magnitude: "%.2g"
     * always fits CALCULATOR_MATH_DISPLAY_CHARS. */
    (void)snprintf(text, size, "%.10g", value);
    if (strlen(text) <= CALCULATOR_MATH_DISPLAY_CHARS)
    {
        return;
    }
    (void)snprintf(text, size, "%.6g", value);
    if (strlen(text) <= CALCULATOR_MATH_DISPLAY_CHARS)
    {
        return;
    }
    (void)snprintf(text, size, "%.2g", value);
}

const char *calculator_math_op_symbol(calculator_math_op_t op)
{
    switch (op)
    {
    case CALCULATOR_MATH_OP_ADD:
        return "+";
    case CALCULATOR_MATH_OP_SUBTRACT:
        return "-";
    case CALCULATOR_MATH_OP_MULTIPLY:
        return "×";
    case CALCULATOR_MATH_OP_DIVIDE:
        return "÷";
    default:
        return "";
    }
}

void calculator_math_init(calculator_math_t *math)
{
    if (math == NULL)
    {
        return;
    }
    memset(math, 0, sizeof(*math));
    _calculator_copy(math->entry, sizeof(math->entry), "0");
    _calculator_copy(math->display, sizeof(math->display), "0");
    math->pending = CALCULATOR_MATH_OP_NONE;
}

void calculator_math_digit(calculator_math_t *math, char digit)
{
    if (math == NULL || digit < '0' || digit > '9')
    {
        return;
    }
    if (math->error)
    {
        calculator_math_init(math);
    }
    if (!math->entry_active)
    {
        _calculator_copy(math->entry, sizeof(math->entry), "0");
        math->entry_active = true;
    }

    const bool leading_zero = strcmp(math->entry, "0") == 0 ||
                              strcmp(math->entry, "-0") == 0;
    const size_t length = strlen(math->entry);
    if (leading_zero)
    {
        const size_t offset = math->entry[0] == '-' ? 1U : 0U;
        math->entry[offset] = digit;
        math->entry[offset + 1U] = '\0';
    }
    else if (_calculator_digit_count(math->entry) <
             CALCULATOR_MATH_MAX_DIGITS && length + 1U < sizeof(math->entry))
    {
        math->entry[length] = digit;
        math->entry[length + 1U] = '\0';
    }
    _calculator_sync_display(math);
}

void calculator_math_decimal(calculator_math_t *math)
{
    if (math == NULL)
    {
        return;
    }
    if (math->error)
    {
        calculator_math_init(math);
    }
    if (!math->entry_active)
    {
        _calculator_copy(math->entry, sizeof(math->entry), "0");
        math->entry_active = true;
    }

    const size_t length = strlen(math->entry);
    if (strchr(math->entry, '.') == NULL &&
            _calculator_digit_count(math->entry) <
            CALCULATOR_MATH_MAX_DIGITS && length + 1U < sizeof(math->entry))
    {
        math->entry[length] = '.';
        math->entry[length + 1U] = '\0';
    }
    _calculator_sync_display(math);
}

void calculator_math_sign(calculator_math_t *math)
{
    if (math == NULL || math->error)
    {
        return;
    }
    if (!_calculator_adopt_display(math))
    {
        return;
    }
    if (strcmp(math->entry, "0") != 0 && strcmp(math->entry, "0.") != 0)
    {
        const size_t length = strlen(math->entry);
        if (math->entry[0] == '-')
        {
            memmove(math->entry, math->entry + 1U, length);
        }
        else if (length + 1U < sizeof(math->entry))
        {
            memmove(math->entry + 1U, math->entry, length + 1U);
            math->entry[0] = '-';
        }
    }
    _calculator_sync_display(math);
}

void calculator_math_percent(calculator_math_t *math)
{
    if (math == NULL || math->error)
    {
        return;
    }
    if (!_calculator_adopt_display(math))
    {
        return;
    }

    char text[CALCULATOR_MATH_ENTRY_BYTES];
    calculator_math_format(strtod(math->entry, NULL) / 100.0, text,
                           sizeof(text));
    _calculator_copy(math->entry, sizeof(math->entry), text);
    _calculator_sync_display(math);
}

void calculator_math_backspace(calculator_math_t *math)
{
    if (math == NULL || math->error || !math->entry_active)
    {
        return;
    }

    size_t length = strlen(math->entry);
    if (length > 0U)
    {
        math->entry[--length] = '\0';
    }
    if (length == 0U || strcmp(math->entry, "-") == 0)
    {
        _calculator_copy(math->entry, sizeof(math->entry), "0");
        math->entry_active = false;
    }
    _calculator_sync_display(math);
}

void calculator_math_clear(calculator_math_t *math)
{
    calculator_math_init(math);
}

void calculator_math_operator(calculator_math_t *math, calculator_math_op_t op)
{
    if (math == NULL || math->error || op <= CALCULATOR_MATH_OP_NONE ||
            op > CALCULATOR_MATH_OP_DIVIDE)
    {
        return;
    }

    if (math->pending != CALCULATOR_MATH_OP_NONE && math->entry_active)
    {
        const char *reason = CALCULATOR_RANGE_TEXT;
        double result = 0.0;
        if (!_calculator_evaluate(math->accumulator,
                                  strtod(math->entry, NULL), math->pending,
                                  &result, &reason))
        {
            _calculator_fail(math, reason);
            return;
        }
        math->accumulator = result;
    }
    else if (math->pending == CALCULATOR_MATH_OP_NONE && math->entry_active)
    {
        /* Without an active entry the accumulator already holds the result of
         * the previous equals, which becomes this operation's left operand. */
        math->accumulator = strtod(math->entry, NULL);
    }

    math->pending = op;
    math->entry_active = false;
    _calculator_copy(math->entry, sizeof(math->entry), "0");
    calculator_math_format(math->accumulator, math->display,
                           sizeof(math->display));
    char operator_text[CALCULATOR_MATH_EXPRESSION_BYTES];
    (void)snprintf(operator_text, sizeof(operator_text), "%s %s",
                   math->display, calculator_math_op_symbol(op));
    _calculator_copy(math->expression, sizeof(math->expression),
                     operator_text);
}

void calculator_math_equals(calculator_math_t *math)
{
    if (math == NULL || math->error)
    {
        return;
    }

    if (math->pending == CALCULATOR_MATH_OP_NONE)
    {
        if (!math->entry_active)
        {
            return;
        }
        math->accumulator = strtod(math->entry, NULL);
        math->entry_active = false;
        calculator_math_format(math->accumulator, math->display,
                               sizeof(math->display));
        return;
    }

    const double rhs = math->entry_active ? strtod(math->entry, NULL) :
                       math->accumulator;
    const char *reason = CALCULATOR_RANGE_TEXT;
    double result = 0.0;
    if (!_calculator_evaluate(math->accumulator, rhs, math->pending, &result,
                              &reason))
    {
        _calculator_fail(math, reason);
        return;
    }

    char lhs_text[CALCULATOR_MATH_DISPLAY_BYTES];
    char rhs_text[CALCULATOR_MATH_DISPLAY_BYTES];
    calculator_math_format(math->accumulator, lhs_text, sizeof(lhs_text));
    calculator_math_format(rhs, rhs_text, sizeof(rhs_text));
    char expression[CALCULATOR_MATH_EXPRESSION_BYTES];
    (void)snprintf(expression, sizeof(expression), "%s %s %s =", lhs_text,
                   calculator_math_op_symbol(math->pending), rhs_text);
    _calculator_copy(math->expression, sizeof(math->expression),
                     expression);

    math->accumulator = result;
    math->pending = CALCULATOR_MATH_OP_NONE;
    math->entry_active = false;
    _calculator_copy(math->entry, sizeof(math->entry), "0");
    calculator_math_format(result, math->display, sizeof(math->display));
}

const char *calculator_math_display(const calculator_math_t *math)
{
    return math != NULL ? math->display : "";
}

const char *calculator_math_expression(const calculator_math_t *math)
{
    return math != NULL ? math->expression : "";
}

bool calculator_math_is_error(const calculator_math_t *math)
{
    return math != NULL && math->error;
}