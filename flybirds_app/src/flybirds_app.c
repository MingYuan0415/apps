#define DBG_TAG "flybirds_app"
#include "mt_log.h"

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "chore_service.h"
#include "flybirds_app_persistence.h"
#include "flybirds_audio.h"
#include "flybirds_game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The field is the 368 x 384 area below the 64 px page header. */
#define FLYBIRDS_STEP_MS           33U
#define FLYBIRDS_TICK_PLAYING_MS   33U
#define FLYBIRDS_TICK_IDLE_MS      100U
#define FLYBIRDS_AUDIO_RETRY_MS    2000U
#define FLYBIRDS_WING_PERIOD_MS    120U
#define FLYBIRDS_SCORE_FLASH_MS    150U
#define FLYBIRDS_HIT_FLASH_MS      180U
#define FLYBIRDS_OVER_SFX_DELAY_MS 420U
#define FLYBIRDS_READY_BREATH_MS   1600U
#define FLYBIRDS_CARD_FADE_MS      180U
#define FLYBIRDS_READY_CARD_WIDTH  260
#define FLYBIRDS_READY_CARD_HEIGHT 132
#define FLYBIRDS_OVER_CARD_WIDTH   300
#define FLYBIRDS_OVER_CARD_HEIGHT  232
#define FLYBIRDS_BIRD_RADIUS       10
#define FLYBIRDS_PIPE_RADIUS       6
#define FLYBIRDS_PIPE_BORDER       2
#define FLYBIRDS_STAR_COUNT        6U

typedef struct flybirds_page_state
{
    app_ui_page_t page;
    flybirds_game_t game;
    lv_obj_t *field;
    lv_obj_t *bird;
    lv_obj_t *wing;
    lv_obj_t *pipes[FLYBIRDS_PIPE_COUNT][2];
    lv_obj_t *score_label;
    lv_obj_t *ready_card;
    lv_obj_t *ready_best;
    lv_obj_t *ready_dot;
    lv_obj_t *over_card;
    lv_obj_t *over_score;
    lv_obj_t *over_best;
    lv_obj_t *pause_button;
    lv_obj_t *pause_sheet;
    lv_obj_t *music_button;
    lv_obj_t *sfx_button;
    lv_timer_t *tick_timer;
    uint16_t rendered_score;
    uint32_t score_flash_until;
    uint32_t hit_flash_until;
    uint32_t over_sfx_at;
    uint32_t audio_retry_at;
    uint32_t field_color;
    uint32_t score_color;
    uint32_t best_score;
    int32_t pipe_x[FLYBIRDS_PIPE_COUNT];
    int16_t pipe_h[FLYBIRDS_PIPE_COUNT][2];
    int32_t bird_y_cached;
    bool music;
    bool sfx;
    bool paused;
    bool wing_open;
    bool over_sfx_pending;
    bool audio_silent;
    bool frame_cache_valid;
    bool pipe_shown[FLYBIRDS_PIPE_COUNT][2];
} flybirds_page_state_t;

_Static_assert(sizeof(flybirds_page_state_t) <= APP_MANAGER_PAGE_STATE_BYTES,
               "Flybirds page state exceeds lifecycle arena slot");

/* Preferences outlive a page instance; only the UI worker touches this copy. */
static flybirds_preferences_t s_preferences;
static bool s_preferences_loaded;

/** One copied preference snapshot handed to the background save worker. */
typedef struct flybirds_save_job
{
    flybirds_preferences_t preferences;
} flybirds_save_job_t;

static void _flybirds_save_submit(void);
static void _flybirds_set_tick_period(flybirds_page_state_t *state,
                                      uint32_t period_ms);
static void _flybirds_again_clicked(lv_event_t *event);
static void _flybirds_back_clicked(lv_event_t *event);

static void _flybirds_save_job(const chore_service_cancel_token_t *cancel,
                               void *argument)
{
    flybirds_save_job_t *job = argument;
    if (job == NULL || chore_service_cancel_pending(cancel))
    {
        return;
    }
    const esp_err_t result = flybirds_app_persistence_save(&job->preferences);
    if (result != ESP_OK)
    {
        LOG_W("preference save failed: %s", esp_err_to_name(result));
    }
}

static void _flybirds_save_release(void *argument)
{
    free(argument);
}

static void _flybirds_save_submit(void)
{
    flybirds_save_job_t *job = calloc(1U, sizeof(*job));
    if (job == NULL)
    {
        LOG_W("preference save skipped: no memory");
        return;
    }
    job->preferences = s_preferences;
    const chore_service_job_t chore =
    {
        .run = _flybirds_save_job,
        .release = _flybirds_save_release,
        .arg = job,
    };
    chore_service_handle_t handle;
    if (chore_service_submit(&chore, &handle) != ESP_OK)
    {
        free(job);
        LOG_W("preference job rejected");
    }
}

static void _flybirds_set_visible(lv_obj_t *object, bool visible)
{
    if (object == NULL)
    {
        return;
    }
    lv_obj_set_hidden(object, !visible);
}

static void _flybirds_style_card(lv_obj_t *card)
{
    lv_obj_remove_style_all(card);
    lv_obj_set_style_radius(card, APP_UI_RADIUS_CARD, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, APP_UI_CARD_PAD, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    app_ui_make_passive(card, false);
}

static lv_obj_t *_flybirds_add_label(lv_obj_t *parent, const char *text,
                                     app_theme_font_id_t font,
                                     uint32_t color, int32_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL)
    {
        return NULL;
    }
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_pos(label, 0, y);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, app_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static void _flybirds_build_field(flybirds_page_state_t *state)
{
    state->field = lv_obj_create(state->page.content);
    lv_obj_remove_style_all(state->field);
    lv_obj_set_size(state->field, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(state->field, lv_color_hex(APP_UI_COLOR_BACKGROUND),
                              0);
    lv_obj_set_style_bg_opa(state->field, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(state->field, 0, 0);
    lv_obj_set_scrollable(state->field, false);
    lv_obj_set_clickable(state->field, true);
    state->field_color = APP_UI_COLOR_BACKGROUND;

    static const int32_t stars[FLYBIRDS_STAR_COUNT][2] =
    {
        { 34, 58 }, { 96, 34 }, { 156, 92 },
        { 232, 46 }, { 300, 78 }, { 332, 140 },
    };
    for (size_t index = 0U; index < (size_t)FLYBIRDS_STAR_COUNT; ++index)
    {
        lv_obj_t *star = lv_obj_create(state->field);
        lv_obj_remove_style_all(star);
        lv_obj_set_size(star, 2, 2);
        lv_obj_set_pos(star, stars[index][0], stars[index][1]);
        lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(star, lv_color_hex(APP_UI_COLOR_SURFACE_HI),
                                  0);
        lv_obj_set_style_bg_opa(star, LV_OPA_COVER, 0);
        app_ui_make_passive(star, false);
    }

    lv_obj_t *ground = lv_obj_create(state->field);
    lv_obj_remove_style_all(ground);
    lv_obj_set_size(ground, LV_PCT(100), FLYBIRDS_GROUND_HEIGHT);
    lv_obj_set_pos(ground, 0, FLYBIRDS_PLAY_HEIGHT);
    lv_obj_set_style_bg_color(ground, lv_color_hex(APP_UI_COLOR_HAIRLINE), 0);
    lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
    app_ui_make_passive(ground, false);

    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        for (size_t part = 0U; part < 2U; ++part)
        {
            lv_obj_t *pipe = lv_obj_create(state->field);
            lv_obj_remove_style_all(pipe);
            lv_obj_set_pos(pipe, 0, 0);
            lv_obj_set_size(pipe, FLYBIRDS_PIPE_WIDTH, 1);
            lv_obj_set_style_radius(pipe, FLYBIRDS_PIPE_RADIUS, 0);
            lv_obj_set_style_bg_color(pipe,
                                      lv_color_hex(APP_UI_COLOR_SURFACE), 0);
            lv_obj_set_style_bg_opa(pipe, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pipe, FLYBIRDS_PIPE_BORDER, 0);
            lv_obj_set_style_border_color(pipe,
                                          lv_color_hex(APP_UI_COLOR_RAIN_TINT),
                                          0);
            app_ui_make_passive(pipe, false);
            _flybirds_set_visible(pipe, false);
            state->pipes[index][part] = pipe;
        }
    }

    state->bird = lv_obj_create(state->field);
    lv_obj_remove_style_all(state->bird);
    lv_obj_set_size(state->bird, FLYBIRDS_BIRD_WIDTH, FLYBIRDS_BIRD_HEIGHT);
    lv_obj_set_style_radius(state->bird, FLYBIRDS_BIRD_RADIUS, 0);
    lv_obj_set_style_bg_color(state->bird, lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_bg_opa(state->bird, LV_OPA_COVER, 0);
    app_ui_make_passive(state->bird, false);

    state->wing = lv_obj_create(state->bird);
    lv_obj_remove_style_all(state->wing);
    lv_obj_set_pos(state->wing, 6, 9);
    lv_obj_set_size(state->wing, 11, 6);
    lv_obj_set_style_radius(state->wing, 3, 0);
    lv_obj_set_style_bg_color(state->wing, lv_color_hex(APP_UI_COLOR_RAIN_HI),
                              0);
    lv_obj_set_style_bg_opa(state->wing, LV_OPA_COVER, 0);
    app_ui_make_passive(state->wing, false);

    lv_obj_t *eye = lv_obj_create(state->bird);
    lv_obj_remove_style_all(eye);
    lv_obj_set_pos(eye, 19, 6);
    lv_obj_set_size(eye, 4, 4);
    lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(eye, lv_color_hex(APP_UI_COLOR_ON_ACCENT), 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    app_ui_make_passive(eye, false);

    lv_obj_t *score = lv_obj_create(state->field);
    lv_obj_remove_style_all(score);
    lv_obj_set_size(score, LV_PCT(100), 88);
    lv_obj_set_pos(score, 0, 4);
    app_ui_make_passive(score, false);

    (void)_flybirds_add_label(score, "得分", APP_THEME_FONT_SMALL,
                              APP_UI_COLOR_MUTED, 0);
    state->score_label = _flybirds_add_label(score, "0", APP_THEME_FONT_HUGE,
        APP_UI_COLOR_TEXT, 22);
}

static void _flybirds_build_ready_card(flybirds_page_state_t *state)
{
    state->ready_card = lv_obj_create(state->field);
    _flybirds_style_card(state->ready_card);
    lv_obj_set_size(state->ready_card, FLYBIRDS_READY_CARD_WIDTH,
                    FLYBIRDS_READY_CARD_HEIGHT);
    /* Sits below the resting bird so the player sees the avatar before the
     * first tap; the ground band stays clear. */
    lv_obj_set_pos(state->ready_card,
                   (FLYBIRDS_FIELD_WIDTH - FLYBIRDS_READY_CARD_WIDTH) / 2,
                   216);

    (void)_flybirds_add_label(state->ready_card, "轻点屏幕起飞",
                              APP_THEME_FONT_HEAD, APP_UI_COLOR_TEXT, 14);
    state->ready_best = _flybirds_add_label(state->ready_card, "最高 0",
                                            APP_THEME_FONT_SMALL,
                                            APP_UI_COLOR_MUTED, 56);

    state->ready_dot = lv_obj_create(state->ready_card);
    lv_obj_remove_style_all(state->ready_dot);
    lv_obj_set_size(state->ready_dot, 10, 10);
    lv_obj_set_pos(state->ready_dot, (FLYBIRDS_READY_CARD_WIDTH -
                                      APP_UI_CARD_PAD * 2 - 10) / 2, 92);
    lv_obj_set_style_radius(state->ready_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(state->ready_dot,
                              lv_color_hex(APP_UI_COLOR_RAIN), 0);
    lv_obj_set_style_bg_opa(state->ready_dot, LV_OPA_COVER, 0);
    app_ui_make_passive(state->ready_dot, false);
}

static void _flybirds_build_over_card(flybirds_page_state_t *state)
{
    state->over_card = lv_obj_create(state->field);
    _flybirds_style_card(state->over_card);
    lv_obj_set_size(state->over_card, FLYBIRDS_OVER_CARD_WIDTH,
                    FLYBIRDS_OVER_CARD_HEIGHT);
    lv_obj_set_pos(state->over_card,
                   (FLYBIRDS_FIELD_WIDTH - FLYBIRDS_OVER_CARD_WIDTH) / 2, 84);

    (void)_flybirds_add_label(state->over_card, "本局得分",
                              APP_THEME_FONT_SMALL, APP_UI_COLOR_MUTED, 12);
    state->over_score = _flybirds_add_label(state->over_card, "0",
                                            APP_THEME_FONT_HUGE,
                                            APP_UI_COLOR_TEXT, 36);
    state->over_best = _flybirds_add_label(state->over_card, "最高 0",
                                           APP_THEME_FONT_SMALL,
                                           APP_UI_COLOR_MUTED, 104);

    lv_obj_t *row = app_ui_button_row_create(state->over_card, 56);
    if (row != NULL)
    {
        lv_obj_set_width(row, FLYBIRDS_OVER_CARD_WIDTH - APP_UI_CARD_PAD * 2);
        lv_obj_set_pos(row, 0, 152);
        lv_obj_t *again = app_ui_button_create(row, "再来一局",
                                               _flybirds_again_clicked, state);
        if (again != NULL)
        {
            app_ui_button_set_kind(again, APP_UI_BUTTON_PRIMARY);
        }
        (void)app_ui_button_create(row, "返回", _flybirds_back_clicked,
                                   state);
    }
    _flybirds_set_visible(state->over_card, false);
}

static void _flybirds_layout_content(flybirds_page_state_t *state)
{
    lv_obj_set_style_pad_all(state->page.content, 0, 0);
    lv_obj_set_style_pad_row(state->page.content, 0, 0);
    lv_obj_set_scrollable(state->page.content, false);
    lv_obj_set_scroll_dir(state->page.content, LV_DIR_NONE);
}

static void _flybirds_update_best_labels(flybirds_page_state_t *state)
{
    char text[32];
    (void)snprintf(text, sizeof(text), "最高 %u",
                   (unsigned)state->best_score);
    app_ui_label_set_text_if(state->ready_best, text);
    app_ui_label_set_text_if(state->over_best, text);
}

static void _flybirds_render_score(flybirds_page_state_t *state,
                                   uint32_t now)
{
    if (!state->frame_cache_valid || state->rendered_score != state->game.score)
    {
        char text[16];
        (void)snprintf(text, sizeof(text), "%u", (unsigned)state->game.score);
        state->rendered_score = state->game.score;
        app_ui_label_set_text_if(state->score_label, text);
    }
    const uint32_t color = now < state->score_flash_until ?
                           APP_UI_COLOR_RAIN : APP_UI_COLOR_TEXT;
    if (!state->frame_cache_valid || color != state->score_color)
    {
        state->score_color = color;
        lv_obj_set_style_text_color(state->score_label, lv_color_hex(color),
                                    0);
    }
}

static void _flybirds_render_pipes(flybirds_page_state_t *state)
{
    const bool show = state->game.phase != FLYBIRDS_PHASE_READY;
    const bool full = !state->frame_cache_valid;
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        const flybirds_pipe_t *pipe = &state->game.pipes[index];
        const int32_t x = (int32_t)pipe->x;
        const int32_t gap_top = (int32_t)flybirds_pipe_gap_top(pipe);
        const int32_t gap_bottom = (int32_t)flybirds_pipe_gap_bottom(pipe);
        const int32_t top_height = gap_top > 0 ? gap_top : 1;
        const int32_t bottom_height =
            FLYBIRDS_PLAY_HEIGHT - gap_bottom > 0 ?
            FLYBIRDS_PLAY_HEIGHT - gap_bottom : 1;
        const bool top_shown = show && gap_top > 0;
        const bool bottom_shown = show && gap_bottom < FLYBIRDS_PLAY_HEIGHT;

        if (full || state->pipe_x[index] != x)
        {
            state->pipe_x[index] = x;
            lv_obj_set_pos(state->pipes[index][0], x, 0);
            lv_obj_set_pos(state->pipes[index][1], x, gap_bottom);
        }
        if (full || state->pipe_h[index][0] != (int16_t)top_height)
        {
            state->pipe_h[index][0] = (int16_t)top_height;
            lv_obj_set_size(state->pipes[index][0], FLYBIRDS_PIPE_WIDTH,
                            top_height);
        }
        if (full || state->pipe_h[index][1] != (int16_t)bottom_height)
        {
            state->pipe_h[index][1] = (int16_t)bottom_height;
            lv_obj_set_size(state->pipes[index][1], FLYBIRDS_PIPE_WIDTH,
                            bottom_height);
        }
        if (full || state->pipe_shown[index][0] != top_shown)
        {
            state->pipe_shown[index][0] = top_shown;
            _flybirds_set_visible(state->pipes[index][0], top_shown);
        }
        if (full || state->pipe_shown[index][1] != bottom_shown)
        {
            state->pipe_shown[index][1] = bottom_shown;
            _flybirds_set_visible(state->pipes[index][1], bottom_shown);
        }
    }
}

static void _flybirds_render_bird(flybirds_page_state_t *state, uint32_t now)
{
    const int32_t y = (int32_t)state->game.bird_y;
    if (!state->frame_cache_valid || y != state->bird_y_cached)
    {
        state->bird_y_cached = y;
        lv_obj_set_pos(state->bird, FLYBIRDS_BIRD_X, y);
    }
    /* A frozen field (paused or over) must not keep flapping. */
    if (state->paused || state->game.phase == FLYBIRDS_PHASE_OVER)
    {
        return;
    }
    const bool open = ((now / FLYBIRDS_WING_PERIOD_MS) & 1U) != 0U;
    if (open == state->wing_open)
    {
        return;
    }
    state->wing_open = open;
    if (open)
    {
        lv_obj_set_pos(state->wing, 6, 6);
        lv_obj_set_size(state->wing, 11, 6);
    }
    else
    {
        lv_obj_set_pos(state->wing, 7, 12);
        lv_obj_set_size(state->wing, 9, 5);
    }
}

static void _flybirds_render_breath(flybirds_page_state_t *state,
                                    uint32_t now)
{
    if (state->ready_dot == NULL || state->paused ||
            state->game.phase != FLYBIRDS_PHASE_READY)
    {
        return;
    }
    /* Quantised to 100 ms so consecutive frames stay identical: a per-frame
     * fade would never let the UI settle for tree/screenshot assertions. */
    const uint32_t steps = FLYBIRDS_READY_BREATH_MS / 100U;
    const uint32_t half = steps / 2U;
    const uint32_t phase = (now / 100U) % steps;
    const uint32_t wave = phase < half ? phase : steps - phase;
    const uint32_t opacity = 40U + (wave * 160U) / half;
    lv_obj_set_style_opa(state->ready_dot,
                         (lv_opa_t)(opacity > 255U ? 255U : opacity), 0);
}

static void _flybirds_render_hit(flybirds_page_state_t *state, uint32_t now)
{
    const uint32_t color = now < state->hit_flash_until ?
                           APP_UI_COLOR_WARNING_TINT :
                           APP_UI_COLOR_BACKGROUND;
    if (color == state->field_color)
    {
        return;
    }
    state->field_color = color;
    lv_obj_set_style_bg_color(state->field, lv_color_hex(color), 0);
}

static void _flybirds_render(flybirds_page_state_t *state, uint32_t now)
{
    _flybirds_render_bird(state, now);
    _flybirds_render_pipes(state);
    _flybirds_render_score(state, now);
    _flybirds_render_hit(state, now);
    _flybirds_render_breath(state, now);

    if (state->over_sfx_pending && now >= state->over_sfx_at)
    {
        state->over_sfx_pending = false;
        flybirds_audio_trigger(FLYBIRDS_SFX_OVER);
    }
    state->frame_cache_valid = true;
}

static void _flybirds_opa_animation(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void _flybirds_fade_in(lv_obj_t *object)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&animation, FLYBIRDS_CARD_FADE_MS);
    lv_anim_set_exec_cb(&animation, _flybirds_opa_animation);
    (void)lv_anim_start(&animation);
}

static void _flybirds_enter_over(flybirds_page_state_t *state, uint32_t now)
{
    state->hit_flash_until = now + FLYBIRDS_HIT_FLASH_MS;
    state->over_sfx_pending = true;
    state->over_sfx_at = now + FLYBIRDS_OVER_SFX_DELAY_MS;
    flybirds_audio_trigger(FLYBIRDS_SFX_HIT);
    _flybirds_set_visible(state->pause_button, false);
    _flybirds_set_tick_period(state, FLYBIRDS_TICK_IDLE_MS);

    char text[16];
    (void)snprintf(text, sizeof(text), "%u", (unsigned)state->game.score);
    app_ui_label_set_text_if(state->over_score, text);
    if (state->game.score > state->best_score)
    {
        state->best_score = state->game.score;
        s_preferences.best_score = state->game.score;
        _flybirds_save_submit();
    }
    _flybirds_update_best_labels(state);

    lv_obj_set_style_opa(state->over_card, LV_OPA_TRANSP, 0);
    _flybirds_set_visible(state->over_card, true);
    _flybirds_fade_in(state->over_card);
}

static void _flybirds_set_tick_period(flybirds_page_state_t *state,
                                      uint32_t period_ms)
{
    if (state->tick_timer != NULL)
    {
        lv_timer_set_period(state->tick_timer, period_ms);
    }
}

static void _flybirds_audio_retry(flybirds_page_state_t *state, uint32_t now)
{
    if (flybirds_audio_open(state->music, state->sfx) == ESP_OK)
    {
        state->audio_silent = false;
        LOG_I("audio stream resumed");
        return;
    }
    state->audio_retry_at = now + FLYBIRDS_AUDIO_RETRY_MS;
}

static void _flybirds_restart(flybirds_page_state_t *state)
{
    state->rendered_score = 0U;
    state->score_flash_until = 0U;
    state->hit_flash_until = 0U;
    state->over_sfx_pending = false;
    state->frame_cache_valid = false;
    _flybirds_set_visible(state->over_card, false);
    _flybirds_set_visible(state->ready_card, false);
    state->paused = false;
    _flybirds_set_visible(state->pause_button, true);
    flybirds_game_start(&state->game);
    flybirds_game_flap(&state->game);
    _flybirds_set_tick_period(state, FLYBIRDS_TICK_PLAYING_MS);
    flybirds_audio_trigger(FLYBIRDS_SFX_FLAP);
}

static void _flybirds_tick(lv_timer_t *timer)
{
    flybirds_page_state_t *state = lv_timer_get_user_data(timer);
    if (state == NULL)
    {
        return;
    }
    const uint32_t now = lv_tick_get();
    if (!state->paused && !state->audio_silent &&
            !flybirds_audio_is_open())
    {
        /* The writer died after a successful open (transport rejected a
         * block); fall back to the same retry cadence as a failed open. */
        state->audio_silent = true;
        state->audio_retry_at = now + FLYBIRDS_AUDIO_RETRY_MS;
        LOG_W("audio stream stopped; retrying");
    }
    if (state->audio_silent && !state->paused &&
            (int32_t)(now - state->audio_retry_at) >= 0)
    {
        _flybirds_audio_retry(state, now);
    }
    if (!state->paused && state->game.phase == FLYBIRDS_PHASE_PLAYING)
    {
        flybirds_game_step(&state->game, FLYBIRDS_STEP_MS);
        if (flybirds_game_take_scored(&state->game))
        {
            state->score_flash_until = now + FLYBIRDS_SCORE_FLASH_MS;
            flybirds_audio_trigger(FLYBIRDS_SFX_SCORE);
        }
        if (flybirds_game_take_died(&state->game))
        {
            _flybirds_enter_over(state, now);
        }
    }
    _flybirds_render(state, now);
}

static void _flybirds_field_pressed(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->paused ||
            state->game.phase == FLYBIRDS_PHASE_OVER)
    {
        return;
    }
    const uint32_t now = lv_tick_get();
    if (state->game.phase == FLYBIRDS_PHASE_READY)
    {
        _flybirds_set_visible(state->ready_card, false);
        _flybirds_set_visible(state->pause_button, true);
        _flybirds_set_tick_period(state, FLYBIRDS_TICK_PLAYING_MS);
    }
    if (state->audio_silent)
    {
        _flybirds_audio_retry(state, now);
    }
    flybirds_game_flap(&state->game);
    flybirds_audio_trigger(FLYBIRDS_SFX_FLAP);
    _flybirds_render(state, now);
}

static void _flybirds_close_sheet(flybirds_page_state_t *state)
{
    if (state->pause_sheet != NULL)
    {
        app_ui_sheet_dismiss(state->pause_sheet);
        state->pause_sheet = NULL;
    }
    state->music_button = NULL;
    state->sfx_button = NULL;
}

static void _flybirds_resume_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    _flybirds_close_sheet(state);
    state->paused = false;
    _flybirds_set_visible(state->pause_button,
                          state->game.phase == FLYBIRDS_PHASE_PLAYING);
}

static void _flybirds_home_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    _flybirds_close_sheet(state);
    app_ui_request_run(APP_MANAGER_ID_HOME);
}

static void _flybirds_music_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    state->music = !state->music;
    s_preferences.music = state->music;
    flybirds_audio_set_music(state->music);
    app_ui_button_set_text(state->music_button,
                           state->music ? "音乐：开" : "音乐：关");
    _flybirds_save_submit();
}

static void _flybirds_sfx_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL)
    {
        return;
    }
    state->sfx = !state->sfx;
    s_preferences.sfx = state->sfx;
    flybirds_audio_set_sfx(state->sfx);
    app_ui_button_set_text(state->sfx_button,
                           state->sfx ? "音效：开" : "音效：关");
    _flybirds_save_submit();
}

static void _flybirds_pause_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state == NULL || state->pause_sheet != NULL)
    {
        return;
    }
    state->paused = true;
    _flybirds_set_visible(state->pause_button, false);
    state->pause_sheet = app_ui_sheet_open(state->page.root, "已暂停",
                                           "音乐与音效可在此关闭");
    if (state->pause_sheet == NULL)
    {
        state->paused = false;
        _flybirds_set_visible(state->pause_button, true);
        return;
    }
    lv_obj_t *resume = app_ui_sheet_add_action(state->pause_sheet, "继续",
        false, _flybirds_resume_clicked, state);
    app_ui_button_set_kind(resume, APP_UI_BUTTON_PRIMARY);
    state->music_button = app_ui_sheet_add_action(state->pause_sheet,
        state->music ? "音乐：开" : "音乐：关", false,
        _flybirds_music_clicked, state);
    state->sfx_button = app_ui_sheet_add_action(state->pause_sheet,
        state->sfx ? "音效：开" : "音效：关", false,
        _flybirds_sfx_clicked, state);
    (void)app_ui_sheet_add_action(state->pause_sheet, "返回主页", false,
                                  _flybirds_home_clicked, state);
}

static void _flybirds_again_clicked(lv_event_t *event)
{
    flybirds_page_state_t *state = lv_event_get_user_data(event);
    if (state != NULL)
    {
        _flybirds_restart(state);
        _flybirds_render(state, lv_tick_get());
    }
}

static void _flybirds_back_clicked(lv_event_t *event)
{
    if (lv_event_get_user_data(event) != NULL)
    {
        app_ui_request_run(APP_MANAGER_ID_HOME);
    }
}

static void _flybirds_start(const app_manager_page_context_t *context)
{
    (void)context;
    if (s_preferences_loaded)
    {
        return;
    }
    const esp_err_t result = flybirds_app_persistence_load(&s_preferences);
    if (result != ESP_OK)
    {
        LOG_W("preference load failed: %s", esp_err_to_name(result));
        flybirds_app_persistence_defaults(&s_preferences);
    }
    s_preferences_loaded = true;
}

static void _flybirds_mount(const app_manager_page_context_t *context)
{
    flybirds_page_state_t *state = context->state;
    memset(state, 0, sizeof(*state));
    state->best_score = s_preferences.best_score;
    state->music = s_preferences.music;
    state->sfx = s_preferences.sfx;
    flybirds_game_reset(&state->game);

    app_ui_page_create(&state->page, "飞鸟", false);
    _flybirds_layout_content(state);
    _flybirds_build_field(state);
    lv_obj_add_event_cb(state->field, _flybirds_field_pressed,
                        LV_EVENT_PRESSED, state);
    _flybirds_build_ready_card(state);
    _flybirds_build_over_card(state);
    state->pause_button = app_ui_page_set_action(&state->page, 0,
        LV_SYMBOL_PAUSE, _flybirds_pause_clicked, state);
    _flybirds_set_visible(state->pause_button, false);
    _flybirds_update_best_labels(state);

    state->tick_timer = lv_timer_create(_flybirds_tick,
                                        FLYBIRDS_TICK_IDLE_MS, state);
    if (state->tick_timer != NULL)
    {
        lv_timer_pause(state->tick_timer);
    }
    _flybirds_render(state, lv_tick_get());
}

static void _flybirds_resume(const app_manager_page_context_t *context)
{
    flybirds_page_state_t *state = context->state;
    const esp_err_t result = flybirds_audio_open(state->music, state->sfx);
    state->audio_silent = result != ESP_OK;
    if (state->audio_silent)
    {
        state->audio_retry_at = lv_tick_get() + FLYBIRDS_AUDIO_RETRY_MS;
        LOG_W("audio unavailable: %s", esp_err_to_name(result));
    }
    if (state->tick_timer != NULL)
    {
        lv_timer_resume(state->tick_timer);
    }
}

static esp_err_t _flybirds_pause(const app_manager_page_context_t *context)
{
    flybirds_page_state_t *state = context->state;
    const esp_err_t result = flybirds_audio_close();
    if (result != ESP_OK)
    {
        return result;
    }
    if (state->tick_timer != NULL)
    {
        lv_timer_pause(state->tick_timer);
    }
    return ESP_OK;
}

static void _flybirds_unmount(const app_manager_page_context_t *context)
{
    flybirds_page_state_t *state = context->state;
    if (state->tick_timer != NULL)
    {
        lv_timer_delete(state->tick_timer);
        state->tick_timer = NULL;
    }
    app_ui_page_destroy(&state->page);
    state->field = NULL;
    state->bird = NULL;
    state->wing = NULL;
    state->score_label = NULL;
    state->ready_card = NULL;
    state->ready_best = NULL;
    state->ready_dot = NULL;
    state->over_card = NULL;
    state->over_score = NULL;
    state->over_best = NULL;
    state->pause_button = NULL;
    state->pause_sheet = NULL;
    state->music_button = NULL;
    state->sfx_button = NULL;
    for (size_t index = 0U; index < FLYBIRDS_PIPE_COUNT; ++index)
    {
        state->pipes[index][0] = NULL;
        state->pipes[index][1] = NULL;
    }
}

static const app_manager_page_ops_t s_flybirds_ops =
{
    .start = _flybirds_start,
    .mount = _flybirds_mount,
    .resume = _flybirds_resume,
    .pause = _flybirds_pause,
    .unmount = _flybirds_unmount,
};

static const app_manager_page_definition_t s_flybirds_definition =
{
    .ops = &s_flybirds_ops,
    .memory_size = sizeof(flybirds_page_state_t),
};

static const app_manager_page_route_t s_flybirds_routes[] =
{
    {.page_id = "root", .definition = &s_flybirds_definition, .user_data = NULL},
};

APP_MANAGER_APP_EXPORT_META(flybirds, APP_IMAGE_FLYBIRDS_ICON, "飞鸟",
                            APP_MANAGER_ID_FLYBIRDS, "root",
                            APP_MANAGER_APP_FLAG_NONE, s_flybirds_routes, 25U,
                            "点击飞行");
