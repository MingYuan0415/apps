# MicroTech built-in application catalog.
#
# This file lists App directories explicitly. Each App owns its source,
# resource, service and board-capability requirements in app.cmake. Product
# manifests select a subset from this catalog; consumers must not maintain
# their own App source lists or use recursive globs.

include_guard(GLOBAL)

set(MT_APP_CATALOG_IDS
    common
    home
    menu
    settings
    setup
    weather
    clock
    calendar
    calculator
    recorder
    files
    level
    flybirds
    music
    diagnostics
)

set(MT_APP_CATALOG_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(mt_app_dir_name app_id out_var)
    if(app_id STREQUAL "common")
        set(app_dir "common")
    else()
        set(app_dir "${app_id}_app")
    endif()
    set(${out_var} "${app_dir}" PARENT_SCOPE)
endfunction()

function(mt_app_load_catalog out_catalog)
    set(catalog "")
    foreach(app_id IN LISTS MT_APP_CATALOG_IDS)
        mt_app_dir_name("${app_id}" app_dir)
        set(manifest "${MT_APP_CATALOG_DIR}/${app_dir}/app.cmake")
        if(NOT EXISTS "${manifest}")
            message(FATAL_ERROR
                "App catalog entry ${app_id} lacks ${manifest}")
        endif()
        include("${manifest}")
        set(catalog "${catalog};${app_id}")
    endforeach()
    set(${out_catalog} "${catalog}" PARENT_SCOPE)
endfunction()

function(mt_app_read app_id out_var)
    mt_app_dir_name("${app_id}" app_dir)
    set(manifest "${MT_APP_CATALOG_DIR}/${app_dir}/app.cmake")
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR "Unknown App '${app_id}': missing ${manifest}")
    endif()

    unset(MT_APP_ID)
    unset(MT_APP_SOURCES)
    unset(MT_APP_INCLUDE_DIRS)
    unset(MT_APP_PRIVATE_REQUIRES)
    unset(MT_APP_CAPABILITIES)
    unset(MT_APP_RESOURCE_MANIFEST)
    unset(MT_APP_REQUIRED)
    unset(MT_APP_HIDDEN)
    unset(MT_APP_RESET_HOOK)
    include("${manifest}")

    foreach(field MT_APP_ID MT_APP_SOURCES MT_APP_REQUIRED)
        if(NOT DEFINED ${field} OR "${${field}}" STREQUAL "")
            message(FATAL_ERROR "${manifest} lacks ${field}")
        endif()
    endforeach()
    if(NOT MT_APP_ID STREQUAL app_id)
        message(FATAL_ERROR
            "${manifest} declares MT_APP_ID=${MT_APP_ID}, expected ${app_id}")
    endif()

    set(required_fields
        MT_APP_ID
        MT_APP_SOURCES
        MT_APP_INCLUDE_DIRS
        MT_APP_PRIVATE_REQUIRES
        MT_APP_CAPABILITIES
        MT_APP_RESOURCE_MANIFEST
        MT_APP_REQUIRED
        MT_APP_HIDDEN
        MT_APP_RESET_HOOK
    )
    foreach(field IN LISTS required_fields)
        set(${field} "${${field}}" PARENT_SCOPE)
    endforeach()
    set(MT_APP_DIR "${app_dir}" PARENT_SCOPE)
    set(${out_var} "${app_id}" PARENT_SCOPE)
endfunction()

# Expand one product selection into the single source/include/resource/dependency
# inventory consumed by firmware, simulator and integration tests.
function(mt_apps_expand selected_apps out_sources out_includes out_requires
         out_resources out_reset_hooks out_ids)
    set(sources "")
    set(includes "")
    set(requires "")
    set(resources "")
    set(reset_hooks "")
    set(ids "")
    foreach(app_id IN LISTS selected_apps)
        mt_app_read("${app_id}" selected_id)
        list(APPEND ids "${selected_id}")
        foreach(source IN LISTS MT_APP_SOURCES)
            list(APPEND sources "${MT_APP_CATALOG_DIR}/${MT_APP_DIR}/${source}")
        endforeach()
        foreach(include_dir IN LISTS MT_APP_INCLUDE_DIRS)
            list(APPEND includes "${MT_APP_CATALOG_DIR}/${MT_APP_DIR}/${include_dir}")
        endforeach()
        foreach(require IN LISTS MT_APP_PRIVATE_REQUIRES)
            list(APPEND requires "${require}")
        endforeach()
        if(MT_APP_RESOURCE_MANIFEST)
            list(APPEND resources
                "${MT_APP_CATALOG_DIR}/${MT_APP_DIR}/${MT_APP_RESOURCE_MANIFEST}")
        endif()
        if(MT_APP_RESET_HOOK)
            list(APPEND reset_hooks "${MT_APP_RESET_HOOK}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES includes)
    list(REMOVE_DUPLICATES requires)
    list(REMOVE_DUPLICATES reset_hooks)
    set(${out_sources} "${sources}" PARENT_SCOPE)
    set(${out_includes} "${includes}" PARENT_SCOPE)
    set(${out_requires} "${requires}" PARENT_SCOPE)
    set(${out_resources} "${resources}" PARENT_SCOPE)
    set(${out_reset_hooks} "${reset_hooks}" PARENT_SCOPE)
    set(${out_ids} "${ids}" PARENT_SCOPE)
endfunction()
