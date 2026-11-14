set(MT_APP_ID recorder)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/recorder_app.c
    src/recorder_control_card.c
    src/recorder_shared.c
    src/recorder_files_page.c
    src/recorder_detail_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    freertos
    lvgl__lvgl
    mt_log
    recorder_service
    sd_storage_service
)
set(MT_APP_CAPABILITIES audio sd)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
