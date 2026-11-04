set(MT_APP_ID flybirds)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/flybirds_app.c
    src/flybirds_app_persistence.c
    src/flybirds_audio.c
    src/flybirds_audio_session.c
    src/flybirds_game.c
    src/flybirds_synth.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    chore_service
    freertos
    lvgl__lvgl
    mt_log
    nv_storage
)
set(MT_APP_CAPABILITIES audio)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK flybirds_app_persistence_reset)
