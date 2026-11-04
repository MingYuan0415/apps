set(MT_APP_ID files)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/files_app.c
    src/files_browse_page.c
    src/files_detail_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    file_service
    freertos
    lvgl__lvgl
    mt_log
    sd_storage_service
)
set(MT_APP_CAPABILITIES sd)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
