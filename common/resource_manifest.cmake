# Shared UI glyph set (app_ui). One SVG source per glyph, baked in the neutral
# INK color and recolored at runtime; no committed PNG exists. Keep this list
# non-recursive and explicit.
list(APPEND MICROTECH_APP_RESOURCE_RECORDS
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_refresh.svg|ui_refresh.png|APP_IMAGE_UI_REFRESH|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_close.svg|ui_close.png|APP_IMAGE_UI_CLOSE|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_check.svg|ui_check.png|APP_IMAGE_UI_CHECK|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_trash.svg|ui_trash.png|APP_IMAGE_UI_TRASH|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_loop.svg|ui_loop.png|APP_IMAGE_UI_LOOP|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_link.svg|ui_link.png|APP_IMAGE_UI_LINK|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_unlink.svg|ui_unlink.png|APP_IMAGE_UI_UNLINK|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_bluetooth.svg|ui_bluetooth.png|APP_IMAGE_UI_BLUETOOTH|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_wifi.svg|ui_wifi.png|APP_IMAGE_UI_WIFI|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_wifi_off.svg|ui_wifi_off.png|APP_IMAGE_UI_WIFI_OFF|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_brightness.svg|ui_brightness.png|APP_IMAGE_UI_BRIGHTNESS|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_power.svg|ui_power.png|APP_IMAGE_UI_POWER|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_standby.svg|ui_standby.png|APP_IMAGE_UI_STANDBY|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_chevron_right.svg|ui_chevron_right.png|APP_IMAGE_UI_CHEVRON_RIGHT|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_info.svg|ui_info.png|APP_IMAGE_UI_INFO|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_warning.svg|ui_warning.png|APP_IMAGE_UI_WARNING|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_play.svg|ui_play.png|APP_IMAGE_UI_PLAY|28|28|SVG"
    "${CMAKE_CURRENT_LIST_DIR}/assets/ui_stop.svg|ui_stop.png|APP_IMAGE_UI_STOP|28|28|SVG")