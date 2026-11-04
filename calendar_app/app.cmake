set(MT_APP_ID calendar)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES src/calendar_app.c src/calendar_math.c)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    freertos
    lvgl__lvgl
    mt_log
    time_service
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
