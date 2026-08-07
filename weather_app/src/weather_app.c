#include "weather_app_internal.h"

static const app_manager_page_route_t s_weather_routes[] =
{
    {
        .page_id = "root",
        .definition = &weather_root_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = WEATHER_PAGE_FORECAST,
        .definition = &weather_forecast_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = WEATHER_PAGE_ALERTS,
        .definition = &weather_alerts_page_definition,
        .user_data = NULL,
    },
    {
        .page_id = WEATHER_PAGE_DETAIL,
        .definition = &weather_alert_detail_page_definition,
        .user_data = NULL,
    },
};

APP_MANAGER_APP_EXPORT(weather, NULL, "天气", APP_MANAGER_ID_WEATHER, "root",
                       APP_MANAGER_APP_FLAG_NONE, s_weather_routes);
