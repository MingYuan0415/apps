#ifndef __CALCULATOR_MATH_H__
#define __CALCULATOR_MATH_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacity of the typed-entry buffer, including the terminator. */
#define CALCULATOR_MATH_ENTRY_BYTES 20U
/** @brief Capacity of the rendered primary-line buffer. */
#define CALCULATOR_MATH_DISPLAY_BYTES 16U
/** @brief Capacity of the rendered expression buffer. */
#define CALCULATOR_MATH_EXPRESSION_BYTES 40U
/** @brief Maximum characters one rendered value may occupy. */
#define CALCULATOR_MATH_DISPLAY_CHARS 12U
/** @brief Maximum digits the user may type into one entry. */
#define CALCULATOR_MATH_MAX_DIGITS 12U

/** @brief Binary operation waiting for its right operand. */
typedef enum
{
    CALCULATOR_MATH_OP_NONE = 0,   /**< No pending operation. */
    CALCULATOR_MATH_OP_ADD,        /**< Addition. */
    CALCULATOR_MATH_OP_SUBTRACT,   /**< Subtraction. */
    CALCULATOR_MATH_OP_MULTIPLY,   /**< Multiplication. */
    CALCULATOR_MATH_OP_DIVIDE,     /**< Division. */
} calculator_math_op_t;

/**
 * @brief Calculator entry, pending operation and rendered lines.
 *
 * The engine owns only text and one accumulator: @c entry holds the digits the
 * user is typing, @c accumulator holds the committed left operand, @c pending
 * holds the operator awaiting its right operand, and @c display /
 * @c expression are the two lines a page renders. Every field is plain data,
 * so the whole state can live in a Page's private arena.
 */
typedef struct calculator_math
{
    char entry[CALCULATOR_MATH_ENTRY_BYTES]; /**< Digits being typed. */
    char display[CALCULATOR_MATH_DISPLAY_BYTES]; /**< Primary line. */
    char expression[CALCULATOR_MATH_EXPRESSION_BYTES]; /**< History line. */
    double accumulator; /**< Committed left operand. */
    calculator_math_op_t pending; /**< Operator awaiting a right operand. */
    bool entry_active; /**< @c entry is being edited and @c display shows it. */
    bool error; /**< Last operation failed; only a reset or digit recovers. */
} calculator_math_t;

/**
 * @brief Reset one calculator to its initial "0" state.
 * @param math receives the reset state; NULL is ignored.
 */
void calculator_math_init(calculator_math_t *math);

/**
 * @brief Append one decimal digit to the current entry.
 *
 * A leading "0" or "-0" is replaced by the digit. While the engine is in the
 * error state the call resets it and starts a fresh entry.
 *
 * @param math is the calculator to update.
 * @param digit is an ASCII digit from '0' to '9'; other values are ignored.
 */
void calculator_math_digit(calculator_math_t *math, char digit);

/**
 * @brief Start or extend a fractional entry.
 * @param math is the calculator to update.
 */
void calculator_math_decimal(calculator_math_t *math);

/**
 * @brief Toggle the sign of the current entry.
 *
 * Without an active entry the displayed result becomes the new entry first, so
 * the edit is visible; a result rendered in scientific notation is left alone.
 *
 * @param math is the calculator to update.
 */
void calculator_math_sign(calculator_math_t *math);

/**
 * @brief Divide the current entry by one hundred.
 * @param math is the calculator to update.
 */
void calculator_math_percent(calculator_math_t *math);

/**
 * @brief Delete the last character of the current entry.
 *
 * Deleting the last remaining character leaves "0" and ends the entry, so the
 * next digit starts a new number. Inactive entries and the error state ignore
 * the call.
 *
 * @param math is the calculator to update.
 */
void calculator_math_backspace(calculator_math_t *math);

/**
 * @brief Reset entry, accumulator, rendered lines and the error state.
 * @param math is the calculator to reset.
 */
void calculator_math_clear(calculator_math_t *math);

/**
 * @brief Arm one binary operation, committing the previous one first.
 *
 * With an operation already pending and an active entry the accumulator is
 * recomputed; without an active entry the pending operator is replaced. A
 * failed division enters the error state instead of arming the new operation.
 *
 * @param math is the calculator to update.
 * @param op is the requested operation; NONE and out-of-range values are
 *           ignored.
 */
void calculator_math_operator(calculator_math_t *math, calculator_math_op_t op);

/**
 * @brief Finish the pending operation and render its result.
 *
 * Without a pending operation an active entry is only finalized into the
 * accumulator; otherwise the operation is evaluated with the current entry, or
 * with the accumulator when no entry is active.
 *
 * @param math is the calculator to update.
 */
void calculator_math_equals(calculator_math_t *math);

/**
 * @brief Render one value into the display line format.
 *
 * Prints the shortest of three fixed precisions that fits
 * CALCULATOR_MATH_DISPLAY_CHARS characters, prints "0" for either zero sign,
 * and never emits a non-finite or truncated number.
 *
 * @param value is the value to render.
 * @param text receives the rendered text.
 * @param size is the capacity of @p text.
 */
void calculator_math_format(double value, char *text, size_t size);

/**
 * @brief Return the operator caption used in the expression line.
 * @param op is the operation to name.
 * @return "+", "-", "×", "÷", or an empty string for NONE and invalid values.
 */
const char *calculator_math_op_symbol(calculator_math_op_t op);

/**
 * @brief Return the primary display line.
 * @param math is the calculator to read; NULL returns an empty string.
 * @return NUL-terminated text owned by @p math.
 */
const char *calculator_math_display(const calculator_math_t *math);

/**
 * @brief Return the expression line describing the pending or finished work.
 * @param math is the calculator to read; NULL returns an empty string.
 * @return NUL-terminated text owned by @p math, or an empty string.
 */
const char *calculator_math_expression(const calculator_math_t *math);

/**
 * @brief Report whether the last operation failed.
 * @param math is the calculator to read; NULL reports false.
 * @return true while the engine waits for a reset or a digit.
 */
bool calculator_math_is_error(const calculator_math_t *math);

#ifdef __cplusplus
}
#endif

#endif /* __CALCULATOR_MATH_H__ */