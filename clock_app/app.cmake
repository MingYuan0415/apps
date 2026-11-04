set(MT_APP_ID clock)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/clock_app.c
    src/clock_countdown_page.c
    src/clock_duration_page.c
    src/clock_stopwatch_page.c
    src/clock_focus_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    freertos
    lvgl__lvgl
    mt_log
    time_service
    timer_service
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
