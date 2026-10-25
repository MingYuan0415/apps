/** @file Pthread-backed task and semaphore fakes for flybirds_audio tests. */

#include "flybirds_freertos_fake.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define FAKE_TASK_SLOTS 8U

struct fake_task
{
    pthread_t thread;
    TaskFunction_t entry;
    void *argument;
    bool used;
};

struct fake_semaphore
{
    pthread_mutex_t lock;
    pthread_cond_t signal;
    unsigned count;
};

static struct fake_task s_tasks[FAKE_TASK_SLOTS];
static pthread_mutex_t s_tasks_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_tasks_changed = PTHREAD_COND_INITIALIZER;
static _Thread_local struct fake_task *s_current_task;

static void _add_ms(struct timespec *time, TickType_t ticks)
{
    time->tv_sec += (time_t)(ticks / 1000U);
    time->tv_nsec += (long)(ticks % 1000U) * 1000000L;
    if (time->tv_nsec >= 1000000000L)
    {
        time->tv_nsec -= 1000000000L;
        time->tv_sec += 1;
    }
}

static void _task_cleanup(void *argument)
{
    struct fake_task *task = argument;
    s_current_task = NULL;
    (void)pthread_mutex_lock(&s_tasks_lock);
    task->used = false;
    (void)pthread_cond_broadcast(&s_tasks_changed);
    (void)pthread_mutex_unlock(&s_tasks_lock);
}

static void *_task_entry(void *argument)
{
    struct fake_task *task = argument;
    (void)pthread_detach(pthread_self());
    s_current_task = task;
    pthread_cleanup_push(_task_cleanup, task);
    task->entry(task->argument);
    pthread_cleanup_pop(1);
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
                                   uint32_t stack_depth, void *argument,
                                   UBaseType_t priority, TaskHandle_t *handle,
                                   BaseType_t core_id)
{
    (void)name;
    (void)stack_depth;
    (void)priority;
    (void)core_id;
    if (entry == NULL || handle == NULL)
    {
        return pdFAIL;
    }
    (void)pthread_mutex_lock(&s_tasks_lock);
    for (size_t index = 0U; index < FAKE_TASK_SLOTS; ++index)
    {
        struct fake_task *task = &s_tasks[index];
        if (task->used)
        {
            continue;
        }
        task->entry = entry;
        task->argument = argument;
        task->used = true;
        *handle = task;
        if (pthread_create(&task->thread, NULL, _task_entry, task) != 0)
        {
            task->used = false;
            *handle = NULL;
            (void)pthread_mutex_unlock(&s_tasks_lock);
            return pdFAIL;
        }
        (void)pthread_mutex_unlock(&s_tasks_lock);
        return pdPASS;
    }
    *handle = NULL;
    (void)pthread_mutex_unlock(&s_tasks_lock);
    return pdFAIL;
}

void vTaskDelete(TaskHandle_t task)
{
    assert(s_current_task != NULL && (task == NULL || task == s_current_task));
    pthread_exit(NULL);
}

size_t flybirds_fake_task_count(void)
{
    size_t count = 0U;
    (void)pthread_mutex_lock(&s_tasks_lock);
    for (size_t index = 0U; index < FAKE_TASK_SLOTS; ++index)
    {
        count += s_tasks[index].used ? 1U : 0U;
    }
    (void)pthread_mutex_unlock(&s_tasks_lock);
    return count;
}

bool flybirds_fake_wait_idle(uint32_t timeout_ms)
{
    struct timespec deadline;
    (void)clock_gettime(CLOCK_REALTIME, &deadline);
    _add_ms(&deadline, timeout_ms);
    (void)pthread_mutex_lock(&s_tasks_lock);
    int result = 0;
    bool busy;
    do
    {
        busy = false;
        for (size_t index = 0U; index < FAKE_TASK_SLOTS; ++index)
        {
            busy = busy || s_tasks[index].used;
        }
        if (!busy || result != 0)
        {
            break;
        }
        result = pthread_cond_timedwait(&s_tasks_changed, &s_tasks_lock, &deadline);
    }
    while (true);
    (void)pthread_mutex_unlock(&s_tasks_lock);
    return !busy;
}

void vTaskDelay(TickType_t ticks)
{
    if (ticks == 0U)
    {
        return;
    }
    struct timespec delay =
    {
        .tv_sec = (time_t)(ticks / 1000U),
        .tv_nsec = (long)(ticks % 1000U) * 1000000L,
    };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
    {
    }
}

TickType_t xTaskGetTickCount(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (TickType_t)((uint64_t)now.tv_sec * 1000U +
                        (uint64_t)now.tv_nsec / 1000000U);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    struct fake_semaphore *semaphore = calloc(1U, sizeof(*semaphore));
    if (semaphore == NULL)
    {
        return NULL;
    }
    if (pthread_mutex_init(&semaphore->lock, NULL) != 0)
    {
        free(semaphore);
        return NULL;
    }
    if (pthread_cond_init(&semaphore->signal, NULL) != 0)
    {
        (void)pthread_mutex_destroy(&semaphore->lock);
        free(semaphore);
        return NULL;
    }
    return semaphore;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    if (semaphore == NULL)
    {
        return pdFALSE;
    }
    (void)pthread_mutex_lock(&semaphore->lock);
    if (semaphore->count == 0U && ticks != 0U)
    {
        if (ticks == portMAX_DELAY)
        {
            while (semaphore->count == 0U)
            {
                (void)pthread_cond_wait(&semaphore->signal, &semaphore->lock);
            }
        }
        else
        {
            struct timespec deadline;
            (void)clock_gettime(CLOCK_REALTIME, &deadline);
            _add_ms(&deadline, ticks);
            while (semaphore->count == 0U &&
                    pthread_cond_timedwait(&semaphore->signal, &semaphore->lock,
                                           &deadline) != ETIMEDOUT)
            {
            }
        }
    }
    BaseType_t result = pdFALSE;
    if (semaphore->count > 0U)
    {
        --semaphore->count;
        result = pdTRUE;
    }
    (void)pthread_mutex_unlock(&semaphore->lock);
    return result;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    if (semaphore == NULL)
    {
        return pdFALSE;
    }
    (void)pthread_mutex_lock(&semaphore->lock);
    const bool was_available = semaphore->count != 0U;
    semaphore->count = 1U;
    (void)pthread_cond_signal(&semaphore->signal);
    (void)pthread_mutex_unlock(&semaphore->lock);
    return was_available ? pdFALSE : pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    if (semaphore == NULL)
    {
        return;
    }
    (void)pthread_mutex_destroy(&semaphore->lock);
    (void)pthread_cond_destroy(&semaphore->signal);
    free(semaphore);
}
