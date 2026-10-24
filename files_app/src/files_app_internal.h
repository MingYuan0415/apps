#ifndef __FILES_APP_INTERNAL_H__
#define __FILES_APP_INTERNAL_H__

#include "app_image_ids.h"
#include "app_manager.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "file_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FILES_PAGE_BROWSE "browse"
#define FILES_PAGE_DETAIL "detail"
#define FILES_PATH_BYTES FILE_SERVICE_PATH_BYTES

/** @brief Allocate page payload outside the arena and UI task stack. */
void *files_payload_alloc(size_t size);
extern const app_manager_page_definition_t files_browse_page_definition;
extern const app_manager_page_definition_t files_detail_page_definition;

/** @brief Retain the versioned selection until the file App stops. */
esp_err_t files_app_select(const file_service_target_t *target);
/** @brief Copy the retained selection without consuming it. */
esp_err_t files_app_copy_selection(file_service_target_t *target);
/** @brief Clear navigation retained by the stopped file App. */
void files_app_clear_selection(void);
/** @brief Mark the selected target deleted so browsing can return to its parent. */
void files_app_selection_deleted(void);
/** @brief Move a deleted current directory to its parent, preserving other paths. */
bool files_app_restore_directory(char *directory);
/** @brief Remove the final path component in place. */
void files_path_parent(char *path);
/** @brief Build a child path; failure never leaves a truncated usable path. */
esp_err_t files_path_join(const char *directory, const char *name,
                          char *out, size_t out_size);
/** @brief Format a byte count for display. */
void files_format_size(uint64_t bytes, char *out, size_t out_size);
/** @brief Format a modification time, or report it as unknown. */
void files_format_time(bool has_time, uint32_t epoch_s, char *out,
                       size_t out_size);
/** @brief Navigate to details using a copied, versioned target. */
void files_request_detail(const file_service_target_t *target);
/** @brief Localized business error without conflating busy and nonempty. */
const char *files_error_text(file_service_error_reason_t reason);

#endif /* __FILES_APP_INTERNAL_H__ */
