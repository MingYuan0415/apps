# QQ Music App resource records. The App icon is exported at the launcher's
# 72 px size; transport and state glyphs use the shared 24-grid ink style.
list(APPEND MICROTECH_APP_RESOURCE_RECORDS
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_app.svg|music_app.png|APP_IMAGE_MUSIC_ICON|72|72|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_prev.svg|music_prev.png|APP_IMAGE_MUSIC_PREV|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_play.svg|music_play.png|APP_IMAGE_MUSIC_PLAY|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_pause.svg|music_pause.png|APP_IMAGE_MUSIC_PAUSE|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_next.svg|music_next.png|APP_IMAGE_MUSIC_NEXT|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/music_stop.svg|music_stop.png|APP_IMAGE_MUSIC_STOP|28|28|SVG")
