/** @file Minimal FreeRTOS critical-section compatibility for app tests. */
#ifndef __APPS_HOST_FREERTOS_H__
#define __APPS_HOST_FREERTOS_H__

/** @brief Host representation of an ESP-IDF critical-section lock. */
typedef int portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))

#endif /* __APPS_HOST_FREERTOS_H__ */
