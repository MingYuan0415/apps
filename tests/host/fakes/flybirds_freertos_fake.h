#ifndef __FLYBIRDS_FREERTOS_FAKE_H__
#define __FLYBIRDS_FREERTOS_FAKE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Count reserved or running task slots, including self-delete cleanup. */
size_t flybirds_fake_task_count(void);

/** @brief Wait until all detached tasks have returned their slots. */
bool flybirds_fake_wait_idle(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __FLYBIRDS_FREERTOS_FAKE_H__ */
