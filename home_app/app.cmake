set(MT_APP_ID home)
set(MT_APP_REQUIRED TRUE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/home_app.c
    src/home_app_persistence.c
    src/home_shortcuts.c
    src/home_shortcuts_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    chore_service
    connectivity_manager
    device_link_service
    event_bus
    lvgl__lvgl
    mt_log
    nv_storage
    power_service
    time_service
    timer_service
    weather_service
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK home_app_persistence_reset)
