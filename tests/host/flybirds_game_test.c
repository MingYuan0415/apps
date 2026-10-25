/**
 * @brief  Host test for the Flybirds gameplay rules.
 * @note   Compiles only flybirds_game.c; no LVGL or ESP-IDF shims are involved.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "flybirds_game.h"

static void _assert_ready(const flybirds_game_t *game)
{
    assert(game->phase == FLYBIRDS_PHASE_READY);
    assert(game->score == 0U);
    assert(fabsf(game->bird_velocity) < 0.001F);
    assert(game->bird_y >= 0.0F);
    assert(game->bird_y + (float)FLYBIRDS_BIRD_HEIGHT <=
           (float)FLYBIRDS_PLAY_HEIGHT);
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        assert(!game->pipes[index].scored);
        assert(game->pipes[index].x <= 0.0F);
    }
}

static void test_reset_is_ready(void)
{
    flybirds_game_t game;
    memset(&game, 0xAA, sizeof(game));
    flybirds_game_reset(&game);
    _assert_ready(&game);
    assert(!flybirds_game_take_scored(&game));
    assert(!flybirds_game_take_died(&game));
}

static void test_flap_starts_and_lifts(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    const float start = game.bird_y;
    flybirds_game_flap(&game);
    assert(game.phase == FLYBIRDS_PHASE_PLAYING);
    assert(game.bird_velocity < 0.0F);
    /* One 33 ms frame rises a couple of pixels before gravity eats the lift. */
    flybirds_game_step(&game, 33U);
    assert(game.bird_y < start);
    assert(game.elapsed_ms == 33U);
}

static void test_gravity_reaches_the_ground(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_flap(&game);
    for (size_t frame = 0U; frame < 240U && game.phase ==
            FLYBIRDS_PHASE_PLAYING; ++frame)
    {
        flybirds_game_step(&game, 33U);
    }
    assert(game.phase == FLYBIRDS_PHASE_OVER);
    assert(fabsf(game.bird_y -
                 ((float)FLYBIRDS_PLAY_HEIGHT -
                  (float)FLYBIRDS_BIRD_HEIGHT)) < 0.001F);
    assert(flybirds_game_take_died(&game));
    assert(!flybirds_game_take_died(&game));
}

static void test_ceiling_is_clamped(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_flap(&game);
    for (size_t frame = 0U; frame < 40U; ++frame)
    {
        flybirds_game_flap(&game);
        flybirds_game_step(&game, 33U);
        assert(game.bird_y >= 0.0F);
        assert(game.phase == FLYBIRDS_PHASE_PLAYING);
    }
}

static void test_pipes_spawn_ahead_and_move_left(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    assert(game.phase == FLYBIRDS_PHASE_PLAYING);
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        const flybirds_pipe_t *pipe = &game.pipes[index];
        assert(pipe->x >= (float)FLYBIRDS_FIELD_WIDTH);
        assert(pipe->gap_center - (float)FLYBIRDS_PIPE_GAP * 0.5F >= 0.0F);
        assert(pipe->gap_center + (float)FLYBIRDS_PIPE_GAP * 0.5F <=
               (float)FLYBIRDS_PLAY_HEIGHT);
        if (index > 0U)
        {
            assert(fabsf((game.pipes[index].x - game.pipes[index - 1U].x) -
                         (float)FLYBIRDS_PIPE_SPACING) < 0.001F);
        }
    }
    const float first = game.pipes[0].x;
    flybirds_game_step(&game, 33U);
    assert(game.pipes[0].x < first);
}

static void test_gap_generation_is_deterministic(void)
{
    flybirds_game_t first;
    flybirds_game_t second;
    flybirds_game_reset(&first);
    flybirds_game_reset(&second);
    flybirds_game_start(&first);
    flybirds_game_start(&second);
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        assert(first.pipes[index].x == second.pipes[index].x);
        assert(first.pipes[index].gap_center ==
               second.pipes[index].gap_center);
    }
}

static void test_scoring_awards_each_pipe_once(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    /* Park two columns far right and push the first just past the bird. */
    game.pipes[0].x = (float)FLYBIRDS_BIRD_X - (float)FLYBIRDS_PIPE_WIDTH -
                      1.0F;
    game.pipes[0].gap_center = 180.0F;
    game.pipes[1].x = 900.0F;
    game.pipes[2].x = 1100.0F;
    flybirds_game_step(&game, 1U);
    assert(game.score == 1U);
    assert(flybirds_game_take_scored(&game));
    assert(!flybirds_game_take_scored(&game));
    flybirds_game_step(&game, 1U);
    assert(game.score == 1U);
}

static void test_pipe_collision_ends_the_run(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    game.pipes[0].x = (float)FLYBIRDS_BIRD_X + 1.0F;
    game.pipes[0].gap_center = 40.0F;
    game.pipes[1].x = 900.0F;
    game.pipes[2].x = 1100.0F;
    flybirds_game_step(&game, 1U);
    assert(game.phase == FLYBIRDS_PHASE_OVER);
    assert(flybirds_game_take_died(&game));
    const float frozen = game.bird_y;
    game.elapsed_ms = 0U;
    flybirds_game_step(&game, 33U);
    assert(game.elapsed_ms == 0U);
    assert(game.bird_y == frozen);
}

static void test_flap_rise_is_about_one_bird_height(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    const float start = game.bird_y;
    flybirds_game_flap(&game);
    for (size_t frame = 0U; frame < 60U && game.bird_velocity < 0.0F; ++frame)
    {
        flybirds_game_step(&game, 33U);
    }
    assert(game.bird_velocity >= 0.0F);
    /* Fixed-step Euler lands a hair under the analytic rise; keep the band
     * wide enough for the integrator and still far from the old 1.8x lift. */
    const float rise = start - game.bird_y;
    assert(rise >= (float)FLYBIRDS_BIRD_HEIGHT * 0.9F);
    assert(rise <= (float)FLYBIRDS_BIRD_HEIGHT * 1.3F);
}

static void test_pipe_hitbox_forgives_the_vertical_edge(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    const float bird_bottom = game.bird_y + (float)FLYBIRDS_BIRD_HEIGHT;
    game.pipes[0].x = (float)FLYBIRDS_BIRD_X + 1.0F;
    game.pipes[1].x = 900.0F;
    game.pipes[2].x = 1100.0F;
    /* The opening sits 2 px below the inset box but above the visual box. */
    game.pipes[0].gap_center = bird_bottom -
                               (float)FLYBIRDS_BIRD_HITBOX_INSET + 2.0F -
                               (float)FLYBIRDS_PIPE_GAP * 0.5F;
    flybirds_game_step(&game, 1U);
    assert(game.phase == FLYBIRDS_PHASE_PLAYING);

    /* Two pixels higher the inset box touches the opening edge. */
    game.pipes[0].gap_center = bird_bottom -
                               (float)FLYBIRDS_BIRD_HITBOX_INSET - 2.0F -
                               (float)FLYBIRDS_PIPE_GAP * 0.5F;
    flybirds_game_step(&game, 1U);
    assert(game.phase == FLYBIRDS_PHASE_OVER);
}

static void test_pipe_hitbox_tolerates_horizontal_brush(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    game.pipes[0].gap_center = 40.0F;
    game.pipes[1].x = 900.0F;
    game.pipes[2].x = 1100.0F;
    /* The visual right edge is 1 px into the bird; the inset box is 2 px
     * clear, so the brush must not end the run. */
    game.pipes[0].x = (float)FLYBIRDS_BIRD_X +
                      (float)FLYBIRDS_BIRD_WIDTH -
                      (float)FLYBIRDS_BIRD_HITBOX_INSET + 2.0F;
    flybirds_game_step(&game, 1U);
    assert(game.phase == FLYBIRDS_PHASE_PLAYING);

    game.pipes[0].x = (float)FLYBIRDS_BIRD_X +
                      (float)FLYBIRDS_BIRD_WIDTH -
                      (float)FLYBIRDS_BIRD_HITBOX_INSET - 1.0F;
    flybirds_game_step(&game, 1U);
    assert(game.phase == FLYBIRDS_PHASE_OVER);
}

static void test_gap_edges_match_the_centre(void)
{
    flybirds_pipe_t pipe = {.x = 10.0F, .gap_center = 150.0F, .scored = false};
    assert(fabsf(flybirds_pipe_gap_top(&pipe) -
                 (150.0F - (float)FLYBIRDS_PIPE_GAP * 0.5F)) < 0.001F);
    assert(fabsf(flybirds_pipe_gap_bottom(&pipe) -
                 (150.0F + (float)FLYBIRDS_PIPE_GAP * 0.5F)) < 0.001F);
    assert(flybirds_pipe_gap_top(NULL) == 0.0F);
    assert(flybirds_pipe_gap_bottom(NULL) == 0.0F);
}

static void test_long_frame_is_bounded(void)
{
    flybirds_game_t game;
    flybirds_game_reset(&game);
    flybirds_game_start(&game);
    flybirds_game_step(&game, 1000U);
    assert(game.elapsed_ms == 100U);
}

int main(void)
{
    test_reset_is_ready();
    test_flap_starts_and_lifts();
    test_gravity_reaches_the_ground();
    test_ceiling_is_clamped();
    test_pipes_spawn_ahead_and_move_left();
    test_gap_generation_is_deterministic();
    test_scoring_awards_each_pipe_once();
    test_pipe_collision_ends_the_run();
    test_flap_rise_is_about_one_bird_height();
    test_pipe_hitbox_forgives_the_vertical_edge();
    test_pipe_hitbox_tolerates_horizontal_brush();
    test_gap_edges_match_the_centre();
    test_long_frame_is_bounded();
    printf("flybirds_game_test: ok\n");
    return 0;
}
