/**
 * @brief Flybirds gameplay rules: fixed-step physics, pipes, scoring, collision.
 *
 * This module is pure C: it never touches LVGL, FreeRTOS, ESP-IDF, or storage,
 * so the page and the host suite drive exactly the same behaviour.
 */

#ifndef __FLYBIRDS_GAME_H__
#define __FLYBIRDS_GAME_H__

#include <stdbool.h>
#include <stdint.h>

/** Playfield width in pixels: the full content area of the page. */
#define FLYBIRDS_FIELD_WIDTH        368
/** Playfield height in pixels: page height minus the 64 px header. */
#define FLYBIRDS_FIELD_HEIGHT       384
/** Inert ground band at the bottom of the playfield. */
#define FLYBIRDS_GROUND_HEIGHT      12
/** Bird collision box width. */
#define FLYBIRDS_BIRD_WIDTH         30
/** Bird collision box height. */
#define FLYBIRDS_BIRD_HEIGHT        24
/** Bird left edge; the bird only moves vertically. */
#define FLYBIRDS_BIRD_X             88
/** Pipe column width. */
#define FLYBIRDS_PIPE_WIDTH         60
/** Number of live pipe columns; three cover the whole travel path. */
#define FLYBIRDS_PIPE_COUNT         3
/** Vertical opening the bird must thread; five bird heights keeps it fair. */
#define FLYBIRDS_PIPE_GAP           126
/** Horizontal distance between neighbouring pipe columns. */
#define FLYBIRDS_PIPE_SPACING       168
/** Constant pipe scroll speed in pixels per second. */
#define FLYBIRDS_PIPE_SPEED         80.0F
/** Downward acceleration in pixels per second squared. */
#define FLYBIRDS_GRAVITY            860.0F
/** Upward velocity applied by one flap, in pixels per second. */
#define FLYBIRDS_FLAP_VELOCITY      (-210.0F)
/** Terminal fall speed so a long drop stays readable. */
#define FLYBIRDS_MAX_FALL_SPEED     310.0F
/** Pipe contacts ignore this band around the visual box, both axes. */
#define FLYBIRDS_BIRD_HITBOX_INSET  3
/** Playable height above the ground band. */
#define FLYBIRDS_PLAY_HEIGHT        (FLYBIRDS_FIELD_HEIGHT - FLYBIRDS_GROUND_HEIGHT)

_Static_assert(FLYBIRDS_BIRD_HITBOX_INSET * 2 < FLYBIRDS_BIRD_WIDTH,
               "Flybirds hitbox inset must leave a positive hit width");
_Static_assert(FLYBIRDS_BIRD_HITBOX_INSET * 2 < FLYBIRDS_BIRD_HEIGHT,
               "Flybirds hitbox inset must leave a positive hit height");
_Static_assert(FLYBIRDS_PIPE_GAP >= FLYBIRDS_BIRD_HEIGHT * 5,
               "Flybirds pipe gap must stay at least five bird heights");

/** Gameplay phase owned by the page state machine. */
typedef enum
{
    FLYBIRDS_PHASE_READY = 0, /**< Waiting for the first tap. */
    FLYBIRDS_PHASE_PLAYING,   /**< Gravity, pipes and scoring are live. */
    FLYBIRDS_PHASE_OVER,      /**< The bird crashed; the field is frozen. */
} flybirds_phase_t;

/** One pipe column: the gap centre moves only when a column recycles. */
typedef struct flybirds_pipe
{
    float x;         /**< Left edge in field coordinates. */
    float gap_center; /**< Vertical centre of the opening. */
    bool scored;     /**< This column already awarded its point. */
} flybirds_pipe_t;

/** Complete gameplay state; the page embeds one instance. */
typedef struct flybirds_game
{
    flybirds_phase_t phase;
    float bird_y;        /**< Bird top edge in field coordinates. */
    float bird_velocity; /**< Vertical speed, positive downwards. */
    /** Time spent in the current run; test observation point, the page keeps
     *  its own animation clock. */
    uint32_t elapsed_ms;
    uint16_t score;      /**< Pipes threaded this run. */
    uint32_t random;     /**< Deterministic gap generator state. */
    flybirds_pipe_t pipes[FLYBIRDS_PIPE_COUNT];
    bool scored_event;   /**< A point was awarded since the last take. */
    bool died_event;     /**< The run ended since the last take. */
} flybirds_game_t;

/**
 * @brief Return the field to the ready phase with no pipes in flight.
 * @param game is the state to reset.
 */
void flybirds_game_reset(flybirds_game_t *game);

/**
 * @brief Start a run: place the pipe columns and enter the playing phase.
 * @param game is the state to start; a playing or over game is reset first.
 */
void flybirds_game_start(flybirds_game_t *game);

/**
 * @brief Apply one upward impulse, starting the run when still ready.
 * @param game is the state to flap.
 */
void flybirds_game_flap(flybirds_game_t *game);

/**
 * @brief Advance the simulation by one frame duration.
 *
 * Durations are clamped to 100 ms so a stalled UI frame cannot tunnel the bird
 * through a pipe. Only the playing phase mutates state.
 *
 * @param game is the state to advance.
 * @param dt_ms is the elapsed time in milliseconds.
 */
void flybirds_game_step(flybirds_game_t *game, uint32_t dt_ms);

/**
 * @brief Consume the pending score edge.
 * @param game is the state to inspect.
 * @return true once per awarded point.
 */
bool flybirds_game_take_scored(flybirds_game_t *game);

/**
 * @brief Consume the pending death edge.
 * @param game is the state to inspect.
 * @return true once per ended run.
 */
bool flybirds_game_take_died(flybirds_game_t *game);

/**
 * @brief Return the current top edge of the opening for one pipe.
 * @param pipe is the column to inspect.
 * @return gap top in field coordinates.
 */
float flybirds_pipe_gap_top(const flybirds_pipe_t *pipe);

/**
 * @brief Return the current bottom edge of the opening for one pipe.
 * @param pipe is the column to inspect.
 * @return gap bottom in field coordinates.
 */
float flybirds_pipe_gap_bottom(const flybirds_pipe_t *pipe);

#endif /* __FLYBIRDS_GAME_H__ */
