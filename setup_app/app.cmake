set(MT_APP_ID setup)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN TRUE)
set(MT_APP_SOURCES src/setup_app.c src/setup_wifi_adapter.c)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    chore_service
    connectivity_manager
    device_link_service
    event_bus
    freertos
    lvgl__lvgl
    mt_log
    onboarding_service
)
set(MT_APP_CAPABILITIES radio)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
