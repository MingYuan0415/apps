#ifndef __HOME_SHORTCUTS_PAGE_H__
#define __HOME_SHORTCUTS_PAGE_H__

#include "app_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Page ID of the Home shortcut picker below the Home app. */
#define HOME_SHORTCUTS_PAGE_ID "shortcuts"

/** Immutable page definition referenced by the Home route table. */
extern const app_manager_page_definition_t home_shortcuts_page_definition;

#ifdef __cplusplus
}
#endif

#endif /* __HOME_SHORTCUTS_PAGE_H__ */
