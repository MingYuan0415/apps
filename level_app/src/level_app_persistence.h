#ifndef __LEVEL_APP_PERSISTENCE_H__
#define __LEVEL_APP_PERSISTENCE_H__

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Load persisted level offsets, using zero when no value exists. */
esp_err_t level_app_persistence_load(float *roll_offset,
                                     float *pitch_offset);

/** @brief Persist level calibration offsets. */
esp_err_t level_app_persistence_save(float roll_offset, float pitch_offset);

/** @brief Delete the persisted level calibration if present. */
esp_err_t level_app_persistence_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __LEVEL_APP_PERSISTENCE_H__ */
