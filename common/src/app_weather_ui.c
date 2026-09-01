#include "app_weather_ui.h"

#include "app_image_ids.h"
#include "app_manager.h"

static uint32_t _app_weather_image_id(uint16_t code, bool small)
{
    uint32_t offset = APP_IMAGE_WEATHER_UNKNOWN_MAIN -
                      APP_IMAGE_WEATHER_CLEAR_DAY_MAIN;
    if (code == 100U)
    {
        offset = 0U;
    }
    else if (code == 150U)
    {
        offset = 1U;
    }
    else if (code >= 101U && code <= 103U)
    {
        offset = 2U;
    }
    else if (code >= 151U && code <= 153U)
    {
        offset = 3U;
    }
    else if (code == 104U)
    {
        offset = 5U;
    }
    else if (code == 305U || code == 309U)
    {
        offset = 6U;
    }
    else if ((code >= 300U && code <= 301U) || code == 306U ||
             code == 313U || (code >= 314U && code <= 316U) ||
             code == 350U || code == 351U || code == 399U)
    {
        offset = 7U;
    }
    else if (code == 307U || code == 308U ||
             (code >= 310U && code <= 312U) ||
             (code >= 317U && code <= 318U))
    {
        offset = 8U;
    }
    else if (code == 304U)
    {
        offset = 10U;
    }
    else if (code >= 302U && code <= 303U)
    {
        offset = 9U;
    }
    else if (code == 404U || code == 405U || code == 406U)
    {
        offset = 14U;
    }
    else if (code == 400U || code == 407U)
    {
        offset = 12U;
    }
    else if (code >= 401U && code <= 403U)
    {
        offset = 13U;
    }
    else if (code == 500U || code == 501U)
    {
        offset = 15U;
    }
    else if (code >= 502U && code <= 515U)
    {
        offset = 16U;
    }
    else if (code == 900U)
    {
        offset = 17U;
    }
    else if (code == 901U)
    {
        offset = 18U;
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
    if (app_manager_get_image(_app_weather_image_id(condition_code, small),
                              &descriptor) != ESP_OK)
    {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    lv_image_set_src(image, descriptor);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    return true;
}
