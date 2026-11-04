set(MT_APP_ID settings)
set(MT_APP_REQUIRED TRUE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/settings_app.c
    src/settings_display_page.c
    src/settings_wifi_page.c
    src/settings_wifi_settle.c
    src/settings_bluetooth_page.c
    src/settings_device_page.c
    src/settings_factory_reset_page.c
    src/settings_storage_page.c
    src/settings_sound_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    chore_service
    connectivity_manager
    device_link_service
    esp_app_format
    event_bus
    factory_reset_service
    file_service
    freertos
    lvgl__lvgl
    mt_log
    power_service
    sd_storage_service
    time_service
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
