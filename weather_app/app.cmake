set(MT_APP_ID weather)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/weather_app.c
    src/weather_ui.c
    src/weather_root_page.c
    src/weather_forecast_page.c
    src/weather_alert_pages.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    event_bus
    freertos
    lvgl__lvgl
    mt_log
    weather_service
)
set(MT_APP_CAPABILITIES radio)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
