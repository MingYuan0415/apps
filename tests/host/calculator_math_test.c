/**
 * @brief  Host test for the hardware-independent calculator engine.
 * @note   Compiles only calculator_math.c; no LVGL or ESP-IDF shims are involved.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calculator_math.h"

static void _type(calculator_math_t *math, const char *text)
{
    for (const char *scan = text; *scan != '\0'; ++scan)
    {
        if (*scan == '.')
        {
            calculator_math_decimal(math);
        }
        else
        {
            calculator_math_digit(math, *scan);
        }
    }
}

static void _assert_display(const calculator_math_t *math, const char *want)
{
    if (strcmp(calculator_math_display(math), want) != 0)
    {
        fprintf(stderr, "display %s, want %s\n",
                calculator_math_display(math), want);
        assert(0);
    }
    assert(!calculator_math_is_error(math));
}

static void _assert_expression(const calculator_math_t *math,
                               const char *want)
{
    assert(strcmp(calculator_math_expression(math), want) == 0);
}

static void test_init_and_digits(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _assert_display(&math, "0");
    _assert_expression(&math, "");

    /* A leading zero is replaced, and a repeated zero never grows the entry. */
    _type(&math, "0");
    _assert_display(&math, "0");
    _type(&math, "00");
    _assert_display(&math, "0");
    _type(&math, "5");
    _assert_display(&math, "5");
    calculator_math_init(&math);
    _type(&math, "1234");
    _assert_display(&math, "1234");

    /* Out-of-range characters never reach the entry. */
    calculator_math_digit(&math, 'a');
    calculator_math_digit(&math, ' ');
    _assert_display(&math, "1234");
}

static void test_digit_capacity(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "12345678901234567890");
    _assert_display(&math, "123456789012");
    assert(strlen(calculator_math_display(&math)) ==
           CALCULATOR_MATH_MAX_DIGITS);
}

static void test_decimal_entry(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    calculator_math_decimal(&math);
    _assert_display(&math, "0.");
    _type(&math, "5");
    _assert_display(&math, "0.5");
    /* A second separator is ignored instead of restarting the fraction. */
    calculator_math_decimal(&math);
    _type(&math, "25");
    _assert_display(&math, "0.525");
}

static void test_backspace(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    /* Deleting an inactive entry does nothing. */
    calculator_math_backspace(&math);
    _assert_display(&math, "0");

    _type(&math, "123");
    calculator_math_backspace(&math);
    _assert_display(&math, "12");
    calculator_math_backspace(&math);
    calculator_math_backspace(&math);
    _assert_display(&math, "0");
    /* The entry is finished, so the next digit starts a new number. */
    _type(&math, "7");
    _assert_display(&math, "7");
}

static void test_sign(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    /* Zero has no sign to toggle. */
    calculator_math_sign(&math);
    _assert_display(&math, "0");

    _type(&math, "5");
    calculator_math_sign(&math);
    _assert_display(&math, "-5");
    calculator_math_sign(&math);
    _assert_display(&math, "5");

    /* The displayed result becomes the new entry. */
    calculator_math_init(&math);
    _type(&math, "5");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    _type(&math, "7");
    calculator_math_equals(&math);
    _assert_display(&math, "12");
    calculator_math_sign(&math);
    _assert_display(&math, "-12");
    _assert_expression(&math, "5 + 7 =");
}

static void test_percent(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "50");
    calculator_math_percent(&math);
    _assert_display(&math, "0.5");
    calculator_math_percent(&math);
    _assert_display(&math, "0.005");

    /* Percent from a displayed result edits the result itself. */
    calculator_math_init(&math);
    _type(&math, "200");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    _type(&math, "3");
    calculator_math_equals(&math);
    _assert_display(&math, "600");
    calculator_math_percent(&math);
    _assert_display(&math, "6");
}

static void test_addition_and_expression(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "7");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    _assert_display(&math, "7");
    _assert_expression(&math, "7 +");
    _type(&math, "8");
    _assert_display(&math, "8");
    calculator_math_equals(&math);
    _assert_display(&math, "15");
    _assert_expression(&math, "7 + 8 =");
}

static void test_subtract_multiply_divide(void)
{
    calculator_math_t math;

    calculator_math_init(&math);
    _type(&math, "9");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_SUBTRACT);
    _type(&math, "4");
    calculator_math_equals(&math);
    _assert_display(&math, "5");
    _assert_expression(&math, "9 - 4 =");

    calculator_math_init(&math);
    _type(&math, "6");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    _type(&math, "7");
    calculator_math_equals(&math);
    _assert_display(&math, "42");
    _assert_expression(&math, "6 × 7 =");

    calculator_math_init(&math);
    _type(&math, "8");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_DIVIDE);
    _type(&math, "2");
    calculator_math_equals(&math);
    _assert_display(&math, "4");
    _assert_expression(&math, "8 ÷ 2 =");
}

/* Operations commit left to right: 2 + 3 then × 4 is (2 + 3) × 4. */
static void test_chained_operators(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "2");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    _type(&math, "3");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    _assert_display(&math, "5");
    _assert_expression(&math, "5 ×");
    _type(&math, "4");
    calculator_math_equals(&math);
    _assert_display(&math, "20");
    _assert_expression(&math, "5 × 4 =");

    /* A repeated operator replaces the pending one without recomputing. */
    calculator_math_init(&math);
    _type(&math, "2");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    _assert_display(&math, "2");
    _assert_expression(&math, "2 ×");
    _type(&math, "3");
    calculator_math_equals(&math);
    _assert_display(&math, "6");
}

static void test_equals_rules(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    /* Equals without a pending operation only finalizes the entry. */
    calculator_math_equals(&math);
    _assert_display(&math, "0");
    _assert_expression(&math, "");

    _type(&math, "42");
    calculator_math_equals(&math);
    _assert_display(&math, "42");
    _assert_expression(&math, "");

    /* With no new operand the accumulator supplies the right-hand side. */
    calculator_math_init(&math);
    _type(&math, "7");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    calculator_math_equals(&math);
    _assert_display(&math, "49");
    _assert_expression(&math, "7 × 7 =");

    /* The finished result is the left operand of the next operation. */
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    _assert_expression(&math, "49 +");
    _type(&math, "1");
    calculator_math_equals(&math);
    _assert_display(&math, "50");
    _assert_expression(&math, "49 + 1 =");
}

static void test_clear(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "12");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    _type(&math, "34");
    calculator_math_clear(&math);
    _assert_display(&math, "0");
    _assert_expression(&math, "");
}

static void test_divide_by_zero(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "5");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_DIVIDE);
    _type(&math, "0");
    calculator_math_equals(&math);
    assert(calculator_math_is_error(&math));
    _assert_expression(&math, "除数不能为零");

    /* Every key except a reset or a digit is inert in the error state. */
    calculator_math_operator(&math, CALCULATOR_MATH_OP_ADD);
    calculator_math_equals(&math);
    calculator_math_percent(&math);
    calculator_math_sign(&math);
    calculator_math_backspace(&math);
    assert(calculator_math_is_error(&math));

    _type(&math, "9");
    _assert_display(&math, "9");
    _assert_expression(&math, "");
}

/* Overflow reaches the same terminal state as a failed division. */
static void test_overflow(void)
{
    calculator_math_t math;
    calculator_math_init(&math);
    _type(&math, "999999999999");
    calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
    calculator_math_equals(&math);

    size_t rounds = 0U;
    while (!calculator_math_is_error(&math) && rounds < 16U)
    {
        calculator_math_operator(&math, CALCULATOR_MATH_OP_MULTIPLY);
        calculator_math_equals(&math);
        ++rounds;
    }
    assert(calculator_math_is_error(&math));
    _assert_expression(&math, "结果超出范围");
}

static void test_format(void)
{
    char text[CALCULATOR_MATH_DISPLAY_BYTES];

    calculator_math_format(0.0, text, sizeof(text));
    assert(strcmp(text, "0") == 0);
    calculator_math_format(-0.0, text, sizeof(text));
    assert(strcmp(text, "0") == 0);
    calculator_math_format(-5.0, text, sizeof(text));
    assert(strcmp(text, "-5") == 0);
    calculator_math_format(0.1 + 0.2, text, sizeof(text));
    assert(strcmp(text, "0.3") == 0);
    calculator_math_format(1.0 / 3.0, text, sizeof(text));
    assert(strcmp(text, "0.3333333333") == 0);
    calculator_math_format(INFINITY, text, sizeof(text));
    assert(strcmp(text, "错误") == 0);
    calculator_math_format(NAN, text, sizeof(text));
    assert(strcmp(text, "错误") == 0);

    /* No magnitude may exceed the display width or lose its leading digits. */
    for (int exponent = -20; exponent <= 20; exponent++)
    {
        const double value = pow(10.0, (double)exponent) * 1.5;
        calculator_math_format(value, text, sizeof(text));
        assert(strlen(text) <= CALCULATOR_MATH_DISPLAY_CHARS);
        const double round_trip = strtod(text, NULL);
        assert(fabs(round_trip - value) <= fabs(value) * 1e-12);
    }
}

static void test_op_symbols(void)
{
    assert(strcmp(calculator_math_op_symbol(CALCULATOR_MATH_OP_ADD), "+") == 0);
    assert(strcmp(calculator_math_op_symbol(CALCULATOR_MATH_OP_SUBTRACT),
                  "-") == 0);
    assert(strcmp(calculator_math_op_symbol(CALCULATOR_MATH_OP_MULTIPLY),
                  "×") == 0);
    assert(strcmp(calculator_math_op_symbol(CALCULATOR_MATH_OP_DIVIDE),
                  "÷") == 0);
    assert(strcmp(calculator_math_op_symbol(CALCULATOR_MATH_OP_NONE), "") == 0);
}

int main(void)
{
    test_init_and_digits();
    test_digit_capacity();
    test_decimal_entry();
    test_backspace();
    test_sign();
    test_percent();
    test_addition_and_expression();
    test_subtract_multiply_divide();
    test_chained_operators();
    test_equals_rules();
    test_clear();
    test_divide_by_zero();
    test_overflow();
    test_format();
    test_op_symbols();
    printf("calculator_math_test: ok\n");
    return 0;
}