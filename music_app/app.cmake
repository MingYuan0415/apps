set(MT_APP_ID music)
set(MT_APP_REQUIRED FALSE)
set(MT_APP_HIDDEN FALSE)
set(MT_APP_SOURCES
    src/music_app.c
    src/music_control_card.c
    src/music_ui_state.c
    src/music_ui.c
    src/music_lyrics_view.c
    src/music_start_page.c
    src/music_sidebar_page.c
    src/music_playlist_page.c
    src/music_player_page.c
    src/music_queue_page.c
    src/music_lyrics_page.c
    src/music_login_method_page.c
    src/music_login_page.c
)
set(MT_APP_INCLUDE_DIRS src)
set(MT_APP_PRIVATE_REQUIRES
    app_core
    app_theme
    audio_service
    event_bus
    freertos
    lvgl__lvgl
    mt_log
    music_service
)
set(MT_APP_CAPABILITIES audio radio)
set(MT_APP_RESOURCE_MANIFEST resource_manifest.cmake)
set(MT_APP_RESET_HOOK "")
