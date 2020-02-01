#pragma once
#include <ompt.h>

namespace romp {
/*
 * ompt callback functions declarations and util functions used 
 * in the callbacks.
 * The function name for callbacks are fixed by openmp rtl.
 */

void on_ompt_callback_implicit_task(
        ompt_scope_endpoint_t endPoint,
        ompt_data_t * parallelData,
        ompt_data_t * taskData,
        unsigned int actualParallelism,
        unsigned int index,
        int flags);

void on_ompt_callback_sync_region(
        ompt_sync_region_t kind,
        ompt_scope_endpoint_t endPoint,
        ompt_data_t *parallelData,
        ompt_data_t *taskData,
        const void* codePtrRa); 

// has signature of ompt_callback_mutex_t
void on_ompt_callback_mutex_acquired(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa);   
     
void on_ompt_callback_mutex_released(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa);   

void on_ompt_callback_work(
        ompt_work_t wsType,
        ompt_scope_endpoint_t endPoint,
        ompt_data_t *parallelData,
        ompt_data_t *taskData,
        uint64_t count,
        const void *codePtrRa);

void on_ompt_callback_parallel_begin(
        ompt_data_t *encounteringTaskData,
        const ompt_frame_t *encounteringTaskFrame,
        ompt_data_t *parallelData,
        unsigned int requestedParallelism,
        int flags,
        const void *codePtrRa);

void on_ompt_callback_parallel_end(
        ompt_data_t *parallelData,
        ompt_data_t *encounteringTaskData,
        int flags,
        const void *codePtrRa);

void on_ompt_callback_task_create(
       ompt_data_t *encounteringTaskData,
       const ompt_frame_t *encounteringTaskFrame,
       ompt_data_t *newTaskData,
       int flags,
       int hasDependences,
       const void *codePtrRa);

void on_ompt_callback_task_schedule(
       ompt_data_t *priorTaskData,
       ompt_task_status_t priorTaskStatus,
       ompt_data_t *nextTaskData);

void on_ompt_callback_dependences(
       ompt_data_t *taskData,
       const ompt_dependence_t *deps,
       int ndeps); 

void on_ompt_callback_thread_begin(
       ompt_thread_t threadType,
       ompt_data_t *threadData);

void on_ompt_callback_thread_end(
       ompt_data_t *threadData);

void on_ompt_callback_dispatch(
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       ompt_dispatch_t kind,
       ompt_data_t instance);

/* 
 * on_ompt_callback_reduction has the type signature ompt_callback_sync_region_t
 * warning: this callback is merged into ompt_callback_sync_region_t 
 */
void on_ompt_callback_reduction(
       ompt_sync_region_t kind,
       ompt_scope_endpoint_t endPoint,
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       const void *codePtrRa);
}
