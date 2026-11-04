set(MT_APP_ID level)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/level_app.c
    src/level_app_persistence.c
    src/level_math.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    chore_service
    freertos
    imu_service
    lvgl__lvgl
    mt_log
    nv_storage
)
set(MT_APP_CAPABILITIES imu)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK level_app_persistence_reset)
