set(MT_APP_ID diagnostics)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN TRUE)
set(MT_APP_SOURCES src/diagnostics_app.c)
set(MT_APP_INCLUDE_DIRS "")
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    connectivity_manager
    device_link_service
    freertos
    imu_service
    lvgl__lvgl
    mt_log
    recorder_service
    sd_storage_service
    time_service
)
set(MT_APP_CAPABILITIES "")
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
