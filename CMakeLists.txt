# MicroTech built-in applications. Keep this list explicit so test fixtures can
# never become production registrations through a recursive glob.
set(APP_SRCS
    "common/src/app_ui.c"
    "home_app/src/home_app.c"
    "menu_app/src/menu_app.c"
    "settings_app/src/settings_app.c"
    "setup_app/src/setup_app.c"
    "setup_app/src/setup_wifi_adapter.c"
)

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "common/include"
        "setup_app/src"
    WHOLE_ARCHIVE
    REQUIRES
        esp_common
    PRIV_REQUIRES
        app_core
        app_theme
        event_bus
        wifi_service
        time_service
        power_service
        freertos
        lvgl__lvgl
        mt_log
)
