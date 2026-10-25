#ifndef __RECORDER_APP_INTERNAL_H__
#define __RECORDER_APP_INTERNAL_H__

#include "app_manager.h"
#include "app_image_ids.h"
#include "app_ui.h"
#include "app_ui_theme.h"
#include "recorder_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RECORDER_PAGE_FILES     "files"
#define RECORDER_PAGE_DETAIL    "detail"

/** @brief Earliest timestamp treated as a calibrated clock (2020-01-01 UTC). */
#define RECORDER_UI_MIN_EPOCH_S 1577836800U

extern const app_manager_page_definition_t recorder_root_page_definition;
extern const app_manager_page_definition_t recorder_files_page_definition;
extern const app_manager_page_definition_t recorder_detail_page_definition;

/**
 * @brief Allocate one page-lifetime payload from the largest available heap.
 * @param size is the byte count to clear before returning.
 * @return Zeroed memory, or NULL when the allocation fails.
 */
void *recorder_payload_alloc(size_t size);
/**
 * @brief Show or hide a control without disturbing flex layout.
 * @param control is the object to toggle.
 * @param visible shows the object when true.
 */
void recorder_ui_set_visible(lv_obj_t *control, bool visible);
/**
 * @brief Format milliseconds as minutes:seconds.
 * @param milliseconds is the duration to render.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void recorder_ui_format_mmss(uint32_t milliseconds, char *output,
                             size_t output_size);
/**
 * @brief Format a byte count with a binary unit suffix.
 * @param bytes is the file size.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void recorder_ui_format_size(uint64_t bytes, char *output, size_t output_size);
/**
 * @brief Format a recording timestamp for lists and detail rows.
 *
 * Uses the recording date when the volume reports one, otherwise the name
 * stem, so the row always identifies the entry.
 *
 * @param file is the metadata to render.
 * @param output receives the text.
 * @param output_size is the output buffer size.
 */
void recorder_ui_format_time(const recorder_service_file_t *file, char *output,
                             size_t output_size);
/**
 * @brief Map a service error to text the user can act on.
 * @param error is the last command result.
 * @return Static description.
 */
const char *recorder_ui_error_text(esp_err_t error);
/**
 * @brief Hand the tapped recording to the detail Page.
 * @param file is the metadata captured by the list row.
 */
void recorder_app_select(const recorder_service_file_t *file);
/**
 * @brief Copy the pending selection for a newly mounted detail Page.
 *
 * The selection is App-scope state and survives this call: screen off
 * unmounts the Page and the wake mounts it again, which must still resolve
 * the same recording.
 *
 * @param file receives the metadata.
 * @return ESP_OK, or ESP_ERR_NOT_FOUND when nothing was selected.
 */
esp_err_t recorder_app_copy_selection(recorder_service_file_t *file);
/** @brief Drop the pending selection when the App stops or the file is gone. */
void recorder_app_clear_selection(void);

#endif /* __RECORDER_APP_INTERNAL_H__ */
