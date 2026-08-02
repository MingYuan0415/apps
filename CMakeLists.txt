# MicroTech built-in applications. Keep this list explicit so test fixtures can
# never become production registrations through a recursive glob.
set(APP_SRCS
    "common/src/app_ui.c"
    "home_app/src/home_app.c"
    "menu_app/src/menu_app.c"
    "menu_app/src/motion_demo.c"
    "menu_app/src/audio_demo.c"
    "menu_app/src/audio_demo_adapter.c"
    "menu_app/src/storage_demo.c"
    "menu_app/src/storage_demo_adapter.c"
    "menu_app/src/clock_demo.c"
    "menu_app/src/clock_demo_adapter.c"
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
    PRIV_INCLUDE_DIRS
        "menu_app/src"
    WHOLE_ARCHIVE
    REQUIRES
        esp_common
    PRIV_REQUIRES
        app_core
        app_theme
        audio_service
        event_bus
        imu_service
        sd_storage_service
        connectivity_manager
        provisioning_service
        time_service
        power_service
        esp_app_format
        esp_hw_support
        fatfs
        freertos
        heap
        lvgl__lvgl
        mt_log
)
