#ifndef __APP_UI_THEME_H__
#define __APP_UI_THEME_H__

/**
 * @brief Shared dark visual language for all pages.
 *
 * Raw values live solely in app_theme_colors.h (Emissive AMOLED palette);
 * these aliases keep the legacy names pages already use. Never add a colour
 * literal in page or widget code.
 */

#include "app_theme_colors.h"

#define APP_UI_COLOR_BACKGROUND  APP_THEME_COLOR_VOID
#define APP_UI_COLOR_SURFACE     APP_THEME_COLOR_PLUME
#define APP_UI_COLOR_SHEET       APP_THEME_COLOR_SHEET
#define APP_UI_COLOR_SURFACE_HI  APP_THEME_COLOR_PLUME_HI
#define APP_UI_COLOR_TRACK       APP_THEME_COLOR_TRACK
#define APP_UI_COLOR_SURFACE_LO  APP_THEME_COLOR_SUNKEN
#define APP_UI_COLOR_HAIRLINE    APP_THEME_COLOR_HAIRLINE
#define APP_UI_COLOR_TEXT        APP_THEME_COLOR_INK
#define APP_UI_COLOR_MUTED       APP_THEME_COLOR_INK_SOFT
#define APP_UI_COLOR_FAINT       APP_THEME_COLOR_INK_FAINT
#define APP_UI_COLOR_ON_ACCENT   APP_THEME_COLOR_ON_ACCENT
#define APP_UI_COLOR_SUN         APP_THEME_COLOR_AMBER
#define APP_UI_COLOR_RAIN        APP_THEME_COLOR_AZURE
#define APP_UI_COLOR_RAIN_HI     APP_THEME_COLOR_AZURE_HI
#define APP_UI_COLOR_SUCCESS     APP_THEME_COLOR_MINT
#define APP_UI_COLOR_WARNING     APP_THEME_COLOR_CORAL
#define APP_UI_COLOR_RAIN_TINT   APP_THEME_COLOR_AZURE_TINT
#define APP_UI_COLOR_SUN_TINT    APP_THEME_COLOR_AMBER_TINT
#define APP_UI_COLOR_SUCCESS_TINT APP_THEME_COLOR_MINT_TINT
#define APP_UI_COLOR_WARNING_TINT APP_THEME_COLOR_CORAL_TINT

/* Shape and spacing scale (4 px grid). */
#define APP_UI_RADIUS_CONTROL    12  /**< Chips, keys, small controls. */
#define APP_UI_RADIUS_CARD       20  /**< Cards and grouped lists. */
#define APP_UI_RADIUS_SHEET      28  /**< Bottom-sheet top corners. */
#define APP_UI_PAGE_MARGIN       16  /**< Horizontal page inset. */
#define APP_UI_CARD_PAD          16  /**< Inner card padding. */
#define APP_UI_GAP               12  /**< Default vertical rhythm. */
#define APP_UI_GAP_TIGHT         8   /**< Within a card / chip row. */

/* Touch targets. */
#define APP_UI_BUTTON_HEIGHT     64  /**< Primary / secondary pill buttons. */
#define APP_UI_ROW_HEIGHT        60  /**< Single-line list row. */
#define APP_UI_ROW_HEIGHT_TALL   72  /**< Two-line list row. */
#define APP_UI_VALUE_ROW_HEIGHT  52  /**< Read-only name/value row. */
#define APP_UI_CHIP_HEIGHT       52  /**< Segmented control / chip row. */

#endif /* __APP_UI_THEME_H__ */
