#pragma once
#include "xrCore.h"
#include <atomic>
#include <cstdint>
#include <functional> 

class XRCORE_API CTaskManager
{
public:
    using TaskFunc = std::function<void()>;
    using TaskRangeFunc = std::function<void(uint32_t start, uint32_t end, uint32_t threadNum)>;
    using CompletionCallback = std::function<void()>;
    
    // Basic task submission
    static void AddTask(TaskFunc func);
    static void AddTaskRange(TaskRangeFunc func, uint32_t setSize, uint32_t minRange = 1);
    
    // Task with completion callback (callback runs after task completes)
    static void AddTaskWithCompletion(TaskFunc func, CompletionCallback onComplete);
    
    // Wait for all tasks to complete
    static void WaitAll();
    
    // Initialize/Destroy the task scheduler
    static void Initialize(uint32_t workerThreadOverride = 0);
    static void Destroy();
    
    // Check if scheduler is running
    static bool IsRunning();
    
    // True when the calling thread is currently inside a CTaskManager task
    // (i.e. a worker/executing thread). Used to avoid nested scheduler waits:
    // inner work should run sequentially so no task waits for tasks that only
    // the already-busy worker pool could run.
    static bool IsInsideTask();

private:
    CTaskManager() = delete;
};