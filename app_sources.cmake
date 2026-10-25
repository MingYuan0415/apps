# MicroTech built-in application sources: the single source of truth.
#
# Included (never registered) by every consumer that compiles this layer:
#   - the firmware component: `layers/apps/CMakeLists.txt`
#   - the simulator: `sim/CMakeLists.txt`
#   - the cross-layer integration suite: `tests/integration/CMakeLists.txt`
# Consumers must not re-declare an app source list; adding an App is a one-line
# edit here.
#
# The list stays explicit on purpose: a recursive glob would let test fixtures
# become production registrations by accident.
set(APP_SRCS
    "common/src/app_ui.c"
    "common/src/app_ui_text.c"
    "common/src/app_ui_pull_refresh.c"
    "common/src/apps_persistence.c"
    "common/src/app_weather_ui.c"
    "home_app/src/home_app.c"
    "clock_app/src/clock_app.c"
    "clock_app/src/clock_countdown_page.c"
    "clock_app/src/clock_duration_page.c"
    "clock_app/src/clock_stopwatch_page.c"
    "clock_app/src/clock_focus_page.c"
    "recorder_app/src/recorder_app.c"
    "recorder_app/src/recorder_shared.c"
    "recorder_app/src/recorder_files_page.c"
    "recorder_app/src/recorder_detail_page.c"
    "files_app/src/files_app.c"
    "files_app/src/files_browse_page.c"
    "files_app/src/files_detail_page.c"
    "level_app/src/level_app.c"
    "level_app/src/level_app_persistence.c"
    "level_app/src/level_math.c"
    "calendar_app/src/calendar_app.c"
    "calendar_app/src/calendar_math.c"
    "calculator_app/src/calculator_app.c"
    "calculator_app/src/calculator_math.c"
    "flybirds_app/src/flybirds_app.c"
    "flybirds_app/src/flybirds_app_persistence.c"
    "flybirds_app/src/flybirds_audio.c"
    "flybirds_app/src/flybirds_audio_session.c"
    "flybirds_app/src/flybirds_game.c"
    "flybirds_app/src/flybirds_synth.c"
    "diagnostics_app/src/diagnostics_app.c"
    "weather_app/src/weather_app.c"
    "weather_app/src/weather_ui.c"
    "weather_app/src/weather_root_page.c"
    "weather_app/src/weather_forecast_page.c"
    "weather_app/src/weather_alert_pages.c"
    "menu_app/src/menu_app.c"
    "settings_app/src/settings_app.c"
    "settings_app/src/settings_display_page.c"
    "settings_app/src/settings_wifi_page.c"
    "settings_app/src/settings_bluetooth_page.c"
    "settings_app/src/settings_device_page.c"
    "settings_app/src/settings_factory_reset_page.c"
    "settings_app/src/settings_storage_page.c"
    "setup_app/src/setup_app.c"
    "setup_app/src/setup_wifi_adapter.c"
)

# Every app-private source directory that carries headers. Derived from APP_SRCS
# so a new App never needs a second edit; the firmware component registers these
# as PRIV_INCLUDE_DIRS and each host consumer sets them as target includes.
set(APP_SRC_INCLUDE_DIRS "")
foreach(_app_src IN LISTS APP_SRCS)
    get_filename_component(_app_dir "${_app_src}" DIRECTORY)
    if(_app_dir IN_LIST APP_SRC_INCLUDE_DIRS)
        continue()
    endif()
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_app_dir}")
        file(GLOB _app_dir_headers "${CMAKE_CURRENT_LIST_DIR}/${_app_dir}/*.h")
        if(_app_dir_headers)
            list(APPEND APP_SRC_INCLUDE_DIRS "${_app_dir}")
        endif()
    endif()
endforeach()
unset(_app_dir)
unset(_app_dir_headers)
