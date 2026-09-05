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
#define APP_UI_COLOR_SURFACE_HI  APP_THEME_COLOR_PLUME_HI
#define APP_UI_COLOR_SURFACE_LO  APP_THEME_COLOR_SUNKEN
#define APP_UI_COLOR_HAIRLINE    APP_THEME_COLOR_HAIRLINE
#define APP_UI_COLOR_TEXT        APP_THEME_COLOR_INK
#define APP_UI_COLOR_MUTED       APP_THEME_COLOR_INK_SOFT
#define APP_UI_COLOR_ON_ACCENT   APP_THEME_COLOR_ON_ACCENT
#define APP_UI_COLOR_SUN         APP_THEME_COLOR_AMBER
#define APP_UI_COLOR_RAIN        APP_THEME_COLOR_AZURE
#define APP_UI_COLOR_SUCCESS     APP_THEME_COLOR_MINT
#define APP_UI_COLOR_WARNING     APP_THEME_COLOR_CORAL

#endif /* __APP_UI_THEME_H__ */
