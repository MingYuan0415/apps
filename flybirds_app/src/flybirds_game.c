#include "flybirds_game.h"

#include <stddef.h>

#define FLYBIRDS_STEP_LIMIT_MS   100U
#define FLYBIRDS_SUBSTEP_MS      16U
#define FLYBIRDS_GAP_MARGIN      16.0F
/* The first column enters view quickly so a run rewards the player early. */
#define FLYBIRDS_FIRST_PIPE_X    (FLYBIRDS_FIELD_WIDTH + 20.0F)
#define FLYBIRDS_RANDOM_SEED     0x1F3D5B79U
#define FLYBIRDS_START_Y \
    (((float)FLYBIRDS_PLAY_HEIGHT - (float)FLYBIRDS_BIRD_HEIGHT) * 0.5F)

static uint32_t _game_next_random(flybirds_game_t *game)
{
    game->random = game->random * 1664525U + 1013904223U;
    return game->random >> 16;
}

static float _game_gap_center(flybirds_game_t *game)
{
    const float low = (float)FLYBIRDS_PIPE_GAP * 0.5F + FLYBIRDS_GAP_MARGIN;
    const float high = (float)FLYBIRDS_PLAY_HEIGHT -
                       (float)FLYBIRDS_PIPE_GAP * 0.5F - FLYBIRDS_GAP_MARGIN;
    const uint32_t span = (uint32_t)(high - low);
    if (span == 0U)
    {
        return low;
    }
    return low + (float)(_game_next_random(game) % span);
}

static void _game_place_pipe(flybirds_game_t *game, flybirds_pipe_t *pipe,
                             float x)
{
    pipe->x = x;
    pipe->gap_center = _game_gap_center(game);
    pipe->scored = false;
}

static void _game_enter_over(flybirds_game_t *game)
{
    if (game->phase == FLYBIRDS_PHASE_PLAYING)
    {
        game->phase = FLYBIRDS_PHASE_OVER;
        game->died_event = true;
    }
}

static void _game_advance(flybirds_game_t *game, float dt)
{
    game->bird_velocity += FLYBIRDS_GRAVITY * dt;
    if (game->bird_velocity > FLYBIRDS_MAX_FALL_SPEED)
    {
        game->bird_velocity = FLYBIRDS_MAX_FALL_SPEED;
    }
    game->bird_y += game->bird_velocity * dt;
    if (game->bird_y < 0.0F)
    {
        game->bird_y = 0.0F;
        if (game->bird_velocity < 0.0F)
        {
            game->bird_velocity = 0.0F;
        }
    }

    /* Pipe contacts use a box inset on all four sides: brushing the visual
     * edge is forgiven, while the ground and the score line keep the full
     * visual box. */
    const float hit_left = (float)FLYBIRDS_BIRD_X +
                           (float)FLYBIRDS_BIRD_HITBOX_INSET;
    const float hit_right = (float)FLYBIRDS_BIRD_X +
                            (float)FLYBIRDS_BIRD_WIDTH -
                            (float)FLYBIRDS_BIRD_HITBOX_INSET;
    const float hit_top = game->bird_y +
                          (float)FLYBIRDS_BIRD_HITBOX_INSET;
    const float hit_bottom = game->bird_y +
                             (float)FLYBIRDS_BIRD_HEIGHT -
                             (float)FLYBIRDS_BIRD_HITBOX_INSET;
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        flybirds_pipe_t *pipe = &game->pipes[index];
        pipe->x -= FLYBIRDS_PIPE_SPEED * dt;
        if (pipe->x + (float)FLYBIRDS_PIPE_WIDTH < 0.0F)
        {
            _game_place_pipe(game, pipe,
                             pipe->x + (float)(FLYBIRDS_PIPE_SPACING *
                                               FLYBIRDS_PIPE_COUNT));
            continue;
        }
        if (!pipe->scored && pipe->x + (float)FLYBIRDS_PIPE_WIDTH <
                (float)FLYBIRDS_BIRD_X)
        {
            pipe->scored = true;
            ++game->score;
            game->scored_event = true;
        }
        const bool horizontal_overlap = pipe->x < hit_right &&
                                        pipe->x + (float)FLYBIRDS_PIPE_WIDTH >
                                        hit_left;
        if (horizontal_overlap &&
                (hit_top < flybirds_pipe_gap_top(pipe) ||
                 hit_bottom > flybirds_pipe_gap_bottom(pipe)))
        {
            _game_enter_over(game);
            return;
        }
    }

    if (game->bird_y + (float)FLYBIRDS_BIRD_HEIGHT >
            (float)FLYBIRDS_PLAY_HEIGHT)
    {
        game->bird_y = (float)FLYBIRDS_PLAY_HEIGHT -
                       (float)FLYBIRDS_BIRD_HEIGHT;
        game->bird_velocity = 0.0F;
        _game_enter_over(game);
    }
}

void flybirds_game_reset(flybirds_game_t *game)
{
    if (game == NULL)
    {
        return;
    }
    game->phase = FLYBIRDS_PHASE_READY;
    game->bird_y = FLYBIRDS_START_Y;
    game->bird_velocity = 0.0F;
    game->elapsed_ms = 0U;
    game->score = 0U;
    game->random = FLYBIRDS_RANDOM_SEED;
    game->scored_event = false;
    game->died_event = false;
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        game->pipes[index].x = 0.0F;
        game->pipes[index].gap_center = 0.0F;
        game->pipes[index].scored = false;
    }
}

void flybirds_game_start(flybirds_game_t *game)
{
    if (game == NULL)
    {
        return;
    }
    flybirds_game_reset(game);
    game->phase = FLYBIRDS_PHASE_PLAYING;
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        _game_place_pipe(game, &game->pipes[index],
                         FLYBIRDS_FIRST_PIPE_X +
                         (float)(FLYBIRDS_PIPE_SPACING * index));
    }
}

void flybirds_game_flap(flybirds_game_t *game)
{
    if (game == NULL || game->phase == FLYBIRDS_PHASE_OVER)
    {
        return;
    }
    if (game->phase == FLYBIRDS_PHASE_READY)
    {
        flybirds_game_start(game);
    }
    game->bird_velocity = FLYBIRDS_FLAP_VELOCITY;
}

void flybirds_game_step(flybirds_game_t *game, uint32_t dt_ms)
{
    if (game == NULL || game->phase != FLYBIRDS_PHASE_PLAYING)
    {
        return;
    }
    uint32_t remaining = dt_ms > FLYBIRDS_STEP_LIMIT_MS ?
                         FLYBIRDS_STEP_LIMIT_MS : dt_ms;
    game->elapsed_ms += remaining;
    while (remaining > 0U && game->phase == FLYBIRDS_PHASE_PLAYING)
    {
        const uint32_t chunk = remaining > FLYBIRDS_SUBSTEP_MS ?
                               FLYBIRDS_SUBSTEP_MS : remaining;
        _game_advance(game, (float)chunk / 1000.0F);
        remaining -= chunk;
    }
}

bool flybirds_game_take_scored(flybirds_game_t *game)
{
    if (game == NULL || !game->scored_event)
    {
        return false;
    }
    game->scored_event = false;
    return true;
}

bool flybirds_game_take_died(flybirds_game_t *game)
{
    if (game == NULL || !game->died_event)
    {
        return false;
    }
    game->died_event = false;
    return true;
}

float flybirds_pipe_gap_top(const flybirds_pipe_t *pipe)
{
    if (pipe == NULL)
    {
        return 0.0F;
    }
    return pipe->gap_center - (float)FLYBIRDS_PIPE_GAP * 0.5F;
}

float flybirds_pipe_gap_bottom(const flybirds_pipe_t *pipe)
{
    if (pipe == NULL)
    {
        return 0.0F;
    }
    return pipe->gap_center + (float)FLYBIRDS_PIPE_GAP * 0.5F;
}
