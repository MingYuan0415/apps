cmake_minimum_required(VERSION 3.16)
project(apps_host_tests LANGUAGES C)

enable_testing()

set(APPS_SANITIZER "none" CACHE STRING
    "Host-test sanitizer: none, address, or thread")
set_property(CACHE APPS_SANITIZER PROPERTY STRINGS none address thread)
find_program(APPS_SETARCH setarch)

function(configure_apps_host_target target)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE
        -include "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.h"
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -UNDEBUG
    )

    if(APPS_SANITIZER STREQUAL "address")
        target_compile_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            -fsanitize=address,undefined
        )
        target_link_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            -fsanitize=address,undefined
        )
    elseif(APPS_SANITIZER STREQUAL "thread")
        target_compile_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            -fsanitize=thread
        )
        target_link_options(${target} PRIVATE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            -fsanitize=thread
        )
    elseif(NOT APPS_SANITIZER STREQUAL "none")
        message(FATAL_ERROR
            "Unsupported APPS_SANITIZER=${APPS_SANITIZER}")
    endif()
endfunction()

function(add_apps_host_test test_name target)
    if(APPS_SANITIZER STREQUAL "thread" AND APPS_SETARCH AND
            CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        add_test(NAME ${test_name} COMMAND
            ${APPS_SETARCH} x86_64 -R $<TARGET_FILE:${target}>)
    else()
        add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${target}>)
    endif()
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 10)
endfunction()

add_executable(app_ui_navigation_test
    app_ui_navigation_test.c
    lvgl_image_fake.c
    ../../common/src/app_ui.c
)

target_include_directories(app_ui_navigation_test PRIVATE
    include
    ../../common/include
    ../../../app_manager/app_core/include
)

target_compile_definitions(app_ui_navigation_test PRIVATE _GNU_SOURCE)
configure_apps_host_target(app_ui_navigation_test)
target_compile_options(app_ui_navigation_test PRIVATE
    -ffunction-sections
    -fdata-sections
)
target_link_options(app_ui_navigation_test PRIVATE
    -Wl,--gc-sections
)

add_apps_host_test(app_ui_navigation_test app_ui_navigation_test)

add_executable(settings_factory_reset_page_test
    settings_factory_reset_page_test.c
    lvgl_image_fake.c
    ../../common/src/app_ui.c
    ../../settings_app/src/settings_factory_reset_page.c
)

target_include_directories(settings_factory_reset_page_test PRIVATE
    include
    ../../common/include
    ../../settings_app/src
    ../../../app_manager/app_core/include
    ../../../middleware/components/factory_reset_service/include
)

target_compile_definitions(settings_factory_reset_page_test PRIVATE _GNU_SOURCE)
configure_apps_host_target(settings_factory_reset_page_test)

add_apps_host_test(settings_factory_reset_page_test
                   settings_factory_reset_page_test)

add_executable(setup_wifi_adapter_test
    setup_wifi_adapter_test.c
    ../../setup_app/src/setup_wifi_adapter.c
)

target_include_directories(setup_wifi_adapter_test PRIVATE
    include
    ../../../middleware/components/event_bus/include
    ../../../middleware/components/connectivity_manager/include
    ../../setup_app/src
)

configure_apps_host_target(setup_wifi_adapter_test)

add_apps_host_test(setup_wifi_adapter_test setup_wifi_adapter_test)
