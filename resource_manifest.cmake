# Explicit application resource records for the current product. The product
# resolver supplies one manifest per selected App; common resources are always
# available because shared UI helpers depend on them.
set(MICROTECH_APP_RESOURCE_RECORDS "")
include("${CMAKE_CURRENT_LIST_DIR}/common/resource_manifest.cmake")
foreach(app_manifest IN LISTS MT_PRODUCT_APP_RESOURCE_MANIFESTS)
    if(NOT app_manifest STREQUAL
            "${CMAKE_CURRENT_LIST_DIR}/common/resource_manifest.cmake")
        include("${app_manifest}")
    endif()
endforeach()
