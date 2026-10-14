# MicroTech built-in applications. The source list lives in app_sources.cmake,
# shared with the simulator and the cross-layer integration suite, so a new App
# is one edit there and never a copy in each consumer.
include("${CMAKE_CURRENT_LIST_DIR}/app_sources.cmake")

idf_component_register(
    SRCS ${APP_SRCS}
    INCLUDE_DIRS
        "."
        "common/include"
        "setup_app/src"
    PRIV_INCLUDE_DIRS
        ${APP_SRC_INCLUDE_DIRS}
    WHOLE_ARCHIVE
    REQUIRES
        esp_common
    PRIV_REQUIRES
        app_core
        app_theme
        audio_service
        chore_service
        event_bus
        imu_service
        sd_storage_service
        connectivity_manager
        device_link_service
        factory_reset_service
        time_service
        timer_service
        recorder_service
        onboarding_service
        weather_service
        power_service
        esp_app_format
        esp_hw_support
        fatfs
        freertos
        heap
        lvgl__lvgl
        mt_log
        nv_storage
)
