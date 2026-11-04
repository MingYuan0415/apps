set(MT_APP_ID common)
set(MT_APP_REQUIRED TRUE)
set(MT_APP_HIDDEN TRUE)
set(MT_APP_SOURCES
    src/app_ui.c
    src/app_ui_text.c
    src/app_ui_pull_refresh.c
    src/apps_persistence.c
    src/app_weather_ui.c
)
set(MT_APP_INCLUDE_DIRS include)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    event_bus
    freertos
    lvgl__lvgl
    mt_log
    nv_storage
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
