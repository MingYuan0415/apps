#include "app_weather_ui.h"

#include "app_image_ids.h"
#include "app_manager.h"

/* Offsets from APP_IMAGE_WEATHER_*_MAIN/SMALL, per the QWeather condition
 * code table: 0 clear-day 1 clear-night 2 partly-day 3 partly-night
 * 4 cloudy 5 overcast 6 drizzle 7 rain 8 heavy-rain 9 thunder 10 hail
 * 11 freezing-rain 12 snow 13 heavy-snow 14 sleet 15 fog 16 haze 17 hot
 * 18 cold 19 unknown. */
#define WX_CLEAR_DAY        0U
#define WX_CLEAR_NIGHT      1U
#define WX_PARTLY_DAY       2U
#define WX_PARTLY_NIGHT     3U
#define WX_CLOUDY           4U
#define WX_OVERCAST         5U
#define WX_DRIZZLE          6U
#define WX_RAIN             7U
#define WX_HEAVY_RAIN       8U
#define WX_THUNDER          9U
#define WX_HAIL             10U
#define WX_FREEZING_RAIN    11U
#define WX_SNOW             12U
#define WX_HEAVY_SNOW       13U
#define WX_SLEET            14U
#define WX_FOG              15U
#define WX_HAZE             16U
#define WX_HOT              17U
#define WX_COLD             18U
#define WX_UNKNOWN          19U

uint32_t app_weather_ui_image_id(uint16_t code, bool small)
{
    uint32_t offset = WX_UNKNOWN;
    if (code == 100U)
    {
        offset = WX_CLEAR_DAY;
    }
    else if (code == 150U)
    {
        offset = WX_CLEAR_NIGHT;
    }
    else if (code == 101U || code == 151U)
    {
        offset = WX_CLOUDY;
    }
    else if (code >= 102U && code <= 103U)
    {
        offset = WX_PARTLY_DAY;
    }
    else if (code >= 152U && code <= 153U)
    {
        offset = WX_PARTLY_NIGHT;
    }
    else if (code == 104U)
    {
        offset = WX_OVERCAST;
    }
    else if (code == 305U || code == 309U)
    {
        offset = WX_DRIZZLE;
    }
    else if (code == 300U || code == 301U || code == 306U || code == 399U)
    {
        offset = WX_RAIN;
    }
    else if (code == 307U || code == 308U ||
             (code >= 310U && code <= 312U))
    {
        offset = WX_HEAVY_RAIN;
    }
    else if ((code >= 302U && code <= 303U) ||
             (code >= 313U && code <= 316U))
    {
        offset = WX_THUNDER;
    }
    else if (code == 304U || (code >= 317U && code <= 318U))
    {
        offset = WX_HAIL;
    }
    else if (code == 350U || code == 351U)
    {
        offset = WX_FREEZING_RAIN;
    }
    else if (code == 400U || code == 405U || code == 499U)
    {
        offset = WX_SNOW;
    }
    else if ((code >= 401U && code <= 403U) || code == 406U)
    {
        offset = WX_HEAVY_SNOW;
    }
    else if (code == 404U)
    {
        offset = WX_SLEET;
    }
    else if (code == 500U || code == 501U)
    {
        offset = WX_FOG;
    }
    else if (code >= 502U && code <= 515U)
    {
        offset = WX_HAZE;
    }
    else if (code == 900U)
    {
        offset = WX_HOT;
    }
    else if (code == 901U)
    {
        offset = WX_COLD;
    }
    return (small ? APP_IMAGE_WEATHER_CLEAR_DAY_SMALL :
            APP_IMAGE_WEATHER_CLEAR_DAY_MAIN) + offset;
}

bool app_weather_ui_set_image(lv_obj_t *image, uint16_t condition_code,
                              bool small)
{
    if (image == NULL)
    {
        return false;
    }
    const lv_image_dsc_t *descriptor = NULL;
    if (app_manager_get_image(app_weather_ui_image_id(condition_code, small),
                              &descriptor) != ESP_OK)
    {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    lv_image_set_src(image, descriptor);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    return true;
}
