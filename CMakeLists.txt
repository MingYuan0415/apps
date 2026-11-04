# MicroTech built-in applications. `MT_PRODUCT_APP_SOURCES` is expanded from
# the selected product manifest by products/products.cmake and is shared with
# the simulator and cross-layer tests. An App not selected by the product is
# neither compiled nor linked.
if(EXISTS "${CMAKE_BINARY_DIR}/mt_product_expansion.cmake")
    include("${CMAKE_BINARY_DIR}/mt_product_expansion.cmake")
endif()
if(NOT DEFINED MT_PRODUCT_APP_SOURCES)
    message(FATAL_ERROR
        "MT_PRODUCT_APP_SOURCES is unavailable; resolve the product before the apps component")
endif()

idf_component_register(
    SRCS ${MT_PRODUCT_APP_SOURCES}
    INCLUDE_DIRS
        "."
        "common/include"
    PRIV_INCLUDE_DIRS
        ${MT_PRODUCT_APP_INCLUDE_DIRS}
    WHOLE_ARCHIVE
    REQUIRES
        esp_common
    PRIV_REQUIRES
        ${MT_PRODUCT_APP_REQUIRES}
        app_core
        app_theme
        esp_app_format
        esp_hw_support
        fatfs
        freertos
        heap
        lvgl__lvgl
        mt_log
        nv_storage
)

if(NOT DEFINED MT_PRODUCT_HEADER OR NOT EXISTS "${MT_PRODUCT_HEADER}")
    message(FATAL_ERROR
        "MT_PRODUCT_HEADER is unavailable; resolve the product before the apps component")
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE
    "SHELL:-include ${MT_PRODUCT_HEADER}")
