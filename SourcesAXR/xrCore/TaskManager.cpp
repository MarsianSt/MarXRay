#include "stdafx.h"
#include "TaskManager.h"
#include "TaskScheduler.h"
#include "xrAsyncLogger.h"
#include <vector>
#include <algorithm>

static constexpr uint32_t SIMPLE_POOL_SIZE = 256;
static constexpr uint32_t RANGE_POOL_SIZE = 128;
static constexpr uint32_t FREE_LIST_EMPTY = UINT32_MAX;
static enki::TaskScheduler g_Scheduler;

template<typename T, uint32_t N>
struct FreeListPool
{
    struct alignas(64) Slot
    {
        T task;
    };

    struct alignas(64) FreeMeta
    {
        uint32_t              nextFree[N];
        std::atomic<uint32_t> freeHead;

        void Init()
        {
            for (uint32_t i = 0; i < N - 1; ++i)
                nextFree[i] = i + 1;
            nextFree[N - 1] = FREE_LIST_EMPTY;
            freeHead.store(0, std::memory_order_relaxed);
        }
    };

    Slot     slots[N];
    FreeMeta meta;

    FreeListPool() { meta.Init(); }

    T* Acquire()
    {
        uint32_t idx = meta.freeHead.load(std::memory_order_relaxed);
        while (idx != FREE_LIST_EMPTY)
        {
            uint32_t next = meta.nextFree[idx];
            if (meta.freeHead.compare_exchange_weak(
                idx, next,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                return &slots[idx].task;
            }
        }
        return nullptr; 

    }

    void Release(T* task)
    {
        const uint32_t idx = static_cast<uint32_t>(
            reinterpret_cast<Slot*>(task) - slots);
        R_ASSERT2(idx < N, "TaskPool::Release — pointer out of range");

        uint32_t head = meta.freeHead.load(std::memory_order_relaxed);
        do {
            meta.nextFree[idx] = head;
        } while (!meta.freeHead.compare_exchange_weak(
            head, idx,
            std::memory_order_release,
            std::memory_order_relaxed));
    }
};

struct SimpleTask;
struct RangeTask;
struct FreeSlotTask;

struct SimpleTask : enki::ITaskSet
{
    CTaskManager::TaskFunc func = nullptr;
    bool                   heapAllocated = false;

    SimpleTask() { m_SetSize = 1; }

    void ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/) override
    {
        if (func) func();
        RecycleOrDelete();
    }

private:
    void RecycleOrDelete(); // Только объявление!
};


struct FreeSlotTask : enki::ITaskSet
{
    RangeTask* owner = nullptr;
    bool       heapAllocated = false;
    enki::Dependency dep;

    FreeSlotTask() { m_SetSize = 1; }
    void ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/) override;
};

struct RangeTask : enki::ITaskSet
{
    CTaskManager::TaskRangeFunc func = nullptr;
    bool                        heapAllocated = false;

    RangeTask() = default;

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
    {
        if (func) func(range.start, range.end, threadnum);
    }
};

static FreeListPool<SimpleTask, SIMPLE_POOL_SIZE> g_SimplePool;
static FreeListPool<RangeTask, RANGE_POOL_SIZE>  g_RangePool;
static FreeListPool<FreeSlotTask, RANGE_POOL_SIZE> g_FreeSlotPool;

inline void SimpleTask::RecycleOrDelete()
{
    if (heapAllocated)
    {
        delete this;
    }
    else
    {
        func = nullptr;
        g_SimplePool.Release(this);
    }
}

inline void FreeSlotTask::ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/)
{
    RangeTask* rt = owner;
    if (rt)
    {
        if (rt->heapAllocated)
        {
            delete rt;
        }
        else
        {
            rt->func = nullptr;
            g_RangePool.Release(rt);
        }
    }
    owner = nullptr;

    if (!heapAllocated)
    {
        g_FreeSlotPool.Release(this);
    }
    else
    {
        delete this;
    }
}

void CTaskManager::AddTask(TaskFunc func)
{
    VERIFY(func);
    SimpleTask* task = g_SimplePool.Acquire();

    if (!task)
    {
        // Heap fallback
        task = new SimpleTask();
        task->heapAllocated = true;
        LogWarning("SimplePool exhausted, heap fallback");
    }
    else
    {
        task->heapAllocated = false;
    }

    task->func = func;
    g_Scheduler.AddTaskSetToPipe(task);
}

void CTaskManager::AddTaskWithCompletion(TaskFunc func, CompletionCallback onComplete)
{
    VERIFY(func);
    
    struct CompletionTask : enki::ITaskSet
    {
        TaskFunc taskFunc;
        CompletionCallback onComplete;
        bool heapAllocated = false;
        
        CompletionTask() { m_SetSize = 1; }
        
        void ExecuteRange(enki::TaskSetPartition, uint32_t) override
        {
            if (taskFunc) taskFunc();
        }
    };
    
    struct CallbackTask : enki::ITaskSet
    {
        CompletionCallback callback;
        bool heapAllocated = false;
        enki::Dependency dep;
        
        CallbackTask() { m_SetSize = 1; }
        
        void ExecuteRange(enki::TaskSetPartition, uint32_t) override
        {
            if (callback) callback();
        }
    };
    
    CompletionTask* mainTask = new CompletionTask();
    mainTask->taskFunc = func;
    mainTask->heapAllocated = true;
    
    CallbackTask* cbTask = new CallbackTask();
    cbTask->callback = onComplete;
    cbTask->heapAllocated = true;
    cbTask->dep.SetDependency(mainTask, cbTask);
    
    g_Scheduler.AddTaskSetToPipe(mainTask);
    g_Scheduler.AddTaskSetToPipe(cbTask);
}

bool CTaskManager::IsRunning()
{
    return g_Scheduler.GetIsRunning();
}

void CTaskManager::AddTaskRange(TaskRangeFunc func, uint32_t setSize, uint32_t minRange)
{
    VERIFY(func);
    VERIFY(setSize > 0);

    RangeTask* task = g_RangePool.Acquire();
    if (!task)
    {
        task = new RangeTask();
        task->heapAllocated = true;
        LogWarning("RangePool exhausted, heap fallback");
    }
    else
    {
        task->heapAllocated = false;
    }

    task->func = func;
    task->m_SetSize = setSize;
    task->m_MinRange = minRange;

    FreeSlotTask* fst = g_FreeSlotPool.Acquire();
    if (!fst)
    {
        fst = new FreeSlotTask();
        fst->heapAllocated = true;
    }
    else
    {
        fst->heapAllocated = false;
    }

    fst->owner = task;
    fst->dep.SetDependency(task, fst);

    g_Scheduler.AddTaskSetToPipe(task);
    g_Scheduler.AddTaskSetToPipe(fst);
}

void CTaskManager::WaitAll()
{
    g_Scheduler.WaitforAll();
}

static uint32_t GetPhysicalCoreCount()
{
    DWORD bufLen = 0;
    if (GetLogicalProcessorInformation(nullptr, &bufLen) && bufLen > 0)
    {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(bufLen / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buf.data(), &bufLen))
        {
            uint32_t physical = 0;
            for (const auto& info : buf)
            {
                if (info.Relationship == RelationProcessorCore)
                    ++physical;
            }
            if (physical > 0) return physical;
        }
    }
    const uint32_t logical = std::thread::hardware_concurrency();
    return std::max(1u, logical / 2);
}

void CTaskManager::Initialize(uint32_t workerThreadOverride)
{
    enki::TaskSchedulerConfig config;
    config.customAllocator.alloc = enki::EnkiAllocFunc;
    config.customAllocator.free = enki::EnkiFreeFunc;

    const uint32_t logicalThreads = std::thread::hardware_concurrency();
    const uint32_t physicalCores = GetPhysicalCoreCount();

    if (workerThreadOverride > 0)
    {
        config.numTaskThreadsToCreate = workerThreadOverride;
    }
    else
    {
        config.numTaskThreadsToCreate = (physicalCores > 2) ? (physicalCores - 2) : 1;
    }

    g_Scheduler.Initialize(config);

    LogInfo("Initialized. Logical: %u, Physical: %u, Workers: %u, Simple: %u, Range: %u, FreeSlot: %u",
        logicalThreads, physicalCores, config.numTaskThreadsToCreate,
        SIMPLE_POOL_SIZE, RANGE_POOL_SIZE, RANGE_POOL_SIZE);
}

void CTaskManager::Destroy()
{
    g_Scheduler.WaitforAllAndShutdown();
    LogInfo("Destroyed.");
}