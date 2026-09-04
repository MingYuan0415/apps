#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_image_ids.h"
#include "app_weather_ui.h"

typedef struct
{
    uint16_t code;
    uint32_t offset;
} icon_case_t;

#define CASE(code, name) \
    { code, APP_IMAGE_WEATHER_##name##_MAIN - APP_IMAGE_WEATHER_CLEAR_DAY_MAIN }

static const icon_case_t s_cases[] =
{
    CASE(100, CLEAR_DAY), CASE(150, CLEAR_NIGHT),
    CASE(101, CLOUDY), CASE(151, CLOUDY),
    CASE(102, PARTLY_DAY), CASE(103, PARTLY_DAY),
    CASE(152, PARTLY_NIGHT), CASE(153, PARTLY_NIGHT),
    CASE(104, OVERCAST),
    CASE(305, DRIZZLE), CASE(309, DRIZZLE),
    CASE(300, RAIN), CASE(301, RAIN), CASE(306, RAIN), CASE(399, RAIN),
    CASE(307, HEAVY_RAIN), CASE(308, HEAVY_RAIN),
    CASE(310, HEAVY_RAIN), CASE(311, HEAVY_RAIN), CASE(312, HEAVY_RAIN),
    CASE(302, THUNDER), CASE(303, THUNDER),
    CASE(313, THUNDER), CASE(314, THUNDER), CASE(315, THUNDER),
    CASE(316, THUNDER),
    CASE(304, HAIL), CASE(317, HAIL), CASE(318, HAIL),
    CASE(350, FREEZING_RAIN), CASE(351, FREEZING_RAIN),
    CASE(400, SNOW), CASE(405, SNOW), CASE(499, SNOW),
    CASE(401, HEAVY_SNOW), CASE(402, HEAVY_SNOW), CASE(403, HEAVY_SNOW),
    CASE(406, HEAVY_SNOW),
    CASE(404, SLEET),
    CASE(500, FOG), CASE(501, FOG),
    CASE(502, HAZE), CASE(503, HAZE), CASE(504, HAZE),
    CASE(507, HAZE), CASE(508, HAZE),
    CASE(900, HOT), CASE(901, COLD),
    CASE(999, UNKNOWN), CASE(0, UNKNOWN), CASE(42, UNKNOWN),
};

int main(void)
{
    for (size_t index = 0U; index < sizeof(s_cases) / sizeof(s_cases[0]);
            index++)
    {
        const icon_case_t *entry = &s_cases[index];
        assert(app_weather_ui_image_id(entry->code, false) ==
               APP_IMAGE_WEATHER_CLEAR_DAY_MAIN + entry->offset);
        assert(app_weather_ui_image_id(entry->code, true) ==
               APP_IMAGE_WEATHER_CLEAR_DAY_SMALL + entry->offset);
    }

    /* Every packed MAIN and SMALL weather id must be reachable, otherwise the
     * resource is dead weight in the res partition. */
    uint32_t reachable = 0U;
    for (uint32_t code = 0U; code <= 999U; code++)
    {
        reachable |= 1U << (app_weather_ui_image_id((uint16_t)code, false) -
                            APP_IMAGE_WEATHER_CLEAR_DAY_MAIN);
    }
    assert(reachable == (1U << 20U) - 1U);
    return 0;
}
