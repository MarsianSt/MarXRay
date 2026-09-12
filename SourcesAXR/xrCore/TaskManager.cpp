#include "stdafx.h"
#undef LOG_MODULE
#define LOG_MODULE "xrTaskManager"
#include "TaskManager.h"
#include "TaskScheduler.h"
#include "xrAsyncLogger.h"
#include <vector>
#include <algorithm>
#include <cstddef>  // ptrdiff_t
#include <mutex>    // FreeListPool locking
#include <atomic>   // intrusive recycle-queue head

static constexpr uint32_t SIMPLE_POOL_SIZE    = 256;
static constexpr uint32_t RANGE_POOL_SIZE     = 128;
static constexpr uint32_t FREE_SLOT_POOL_SIZE = RANGE_POOL_SIZE;
static constexpr uint32_t FREE_LIST_EMPTY = UINT32_MAX;
static enki::TaskScheduler g_Scheduler;

// ------------------------------------------------------------------
// Deferred recycling.
// A task is NEVER recycled or deleted from inside its own ExecuteRange:
// after ExecuteRange returns, enkiTS still touches the task object (the
// final m_RunningCount.fetch_sub and possibly TaskComplete). Finished
// tasks publish themselves into the intrusive lock-free list below and
// are actually reclaimed from CTaskManager::WaitAll()/Destroy(), i.e. at
// a point where no scheduler thread can be touching them any more.
//
// The list is a simple Treiber-style stack over an atomic head. No tagged
// (ABA-protected) pointer is needed because a task object is published
// exactly once per submission - its ExecuteRange runs exactly once, and
// re-submission is only possible after the previous FlushRecycledTasks()
// has reclaimed the object - so a node can never be pushed twice while an
// earlier publish of the same object is still pending.
//
// Contract: every batch of AddTask/AddTaskRange calls MUST be closed with a
// CTaskManager::WaitAll() before the next batch (as all current callers do -
// xrFS.cpp:263/:607, xrArchiver.cpp:158/:376). The queue is drained only there
// and in Destroy(). Tasks parked in the queue keep their pool slot (or a heap
// object once the pools overflow) until then; an endless Add* stream without
// WaitAll would grow the queue without bound, so WaitAll is a contract, not an
// optimization. A worker-side or self-flush is deliberately NOT performed: a
// task publishes itself from inside its own ExecuteRange, BEFORE the
// scheduler's final m_RunningCount.fetch_sub and possible TaskComplete
// (TaskScheduler.cpp:TryRunTask), so flushing while any worker thread may
// still touch a task would recycle objects that are still in use. Destroy()
// is safe for the same reason: WaitforAllAndShutdown stops and joins the
// workers before the final flush, so the atomic exchange in FlushRecycledTasks
// never races a PushRecycle.
// Contract violations are caught by R_ASSERT2 in Debug and a one-shot
// LogWarning in Release (see g_RecycleCount).
// ------------------------------------------------------------------
struct RecyclableTask
{
    RecyclableTask* nextRecycle = nullptr;

    // Virtual dtor: not exercised today - each Recycle() reclaims with `delete
    // this` on the concrete type, never through a base pointer - but it costs
    // nothing (the class already carries a vtable for the pure virtual Recycle)
    // and makes base-pointer delete safe if such code ever appears.
    virtual ~RecyclableTask() = default;
    virtual void Recycle() = 0;
};

static std::atomic<RecyclableTask*> g_RecycleHead{ nullptr };

// Recycle-queue accounting: how many finished tasks are parked between two
// WaitAll()/Destroy() calls. Incremented once per PushRecycle, decremented in
// FlushRecycledTasks by the number of nodes it drained; at quiescence (the only
// points flush runs) it equals the queue length, so it doubles as the
// "a WaitAll was skipped" detector in AddTask/AddTaskRange below.
static std::atomic<uint32_t> g_RecycleCount{ 0 };

// Contract-guard limits. Every AddTask/AddTaskRange opens a batch and must be
// closed with WaitAll(); the queue is drained there, so the number of parked
// nodes between two WaitAll() calls equals the in-flight batch size. No current
// caller (xrFS.cpp:252/:597, xrArchiver.cpp:131/:345) ever has more than one
// Add* call pending at a time (<= 2 nodes), so today's legal maximum is ~2.
// HARD_LIMIT sits far above that and at 2x the combined pool sizes
// (256+128+128): a legitimate burst can never reach it, but a loop that keeps
// adding without WaitAll blows past it in microseconds. SOFT_LIMIT is half of
// HARD_LIMIT and arms the one-shot Release warning. The checks belong in
// AddTask/AddTaskRange because those are the batch entry points - a violation
// is detected at the START of the offending next batch.
static constexpr uint32_t HARD_LIMIT = 1024;
static constexpr uint32_t SOFT_LIMIT = HARD_LIMIT / 2;
static std::atomic<bool>  g_RecycleSoftWarned{ false };

// Nodes are published with a release RMW so the flusher's acquire exchange
// observes this node's link and every task field written before the push
// (func, heapAllocated, ownerToRecycleAfter, dep, ...).
static void PushRecycle(RecyclableTask* task)
{
    RecyclableTask* head = g_RecycleHead.load(std::memory_order_relaxed);
    do
    {
        task->nextRecycle = head;
    } while (!g_RecycleHead.compare_exchange_weak(
        head, task,
        std::memory_order_release,
        std::memory_order_relaxed));
    // Counter is bumped after the push succeeded. Relaxed is enough: it feeds a
    // heuristic batch-contract check, not any ordering guarantee of the queue.
    g_RecycleCount.fetch_add(1, std::memory_order_relaxed);
}

static void FlushRecycledTasks()
{
    if (g_RecycleHead.load(std::memory_order_relaxed) == nullptr)
    {
        // Relaxed read is correct, not just fast: flush runs only at
        // WaitAll()/Destroy(), after enkiTS's own acquire/release handshake
        // (TaskScheduler.cpp:WaitforAll spins until no worker is RUNNING and
        // no pipe holds a task). Every PushRecycle release-published before
        // that wait happens-before this thread, so write-read coherence forces
        // this load to observe it - a null read really means the queue is empty.
        return; // common case: nothing to reclaim
    }

    RecyclableTask* task = g_RecycleHead.exchange(nullptr, std::memory_order_acquire);
    uint32_t drained = 0;
    while (task)
    {
        RecyclableTask* next = task->nextRecycle; // read before the node is reclaimed
        task->Recycle();
        ++drained;
        task = next;
    }
    // Decrement by exactly the number of nodes drained here. Flush only runs at
    // quiescence (see the header comment), so no PushRecycle can interleave:
    // the stolen list is the whole queue, the counter matches it, and both go
    // down to 0 together.
    g_RecycleCount.fetch_sub(drained, std::memory_order_relaxed);
    R_ASSERT2(g_RecycleCount.load(std::memory_order_relaxed) == 0,
        "Recycle counter out of sync: queue drained but count is nonzero");
}

template<typename T, uint32_t N>
struct FreeListPool
{
    struct alignas(64) Slot
    {
        T task;
    };

    // Free list guarded by a mutex (see freeMutex). A lock-free pop/push would
    // need 64-bit tagged slots to be ABA-safe; with N <= 256 the mutex is the
    // simpler and equally fast choice at our (rare) contention points.
    struct alignas(64) FreeMeta
    {
        uint32_t nextFree[N];
        uint32_t freeHead = 0;

        void Init()
        {
            for (uint32_t i = 0; i < N - 1; ++i)
                nextFree[i] = i + 1;
            nextFree[N - 1] = FREE_LIST_EMPTY;
            freeHead = 0;
        }
    };

    Slot        slots[N];
    FreeMeta    meta;
    std::mutex  freeMutex;

    FreeListPool() { meta.Init(); }

    T* Acquire()
    {
        std::lock_guard<std::mutex> lock(freeMutex);
        const uint32_t idx = meta.freeHead;
        if (idx == FREE_LIST_EMPTY)
            return nullptr;
        meta.freeHead = meta.nextFree[idx];
        return &slots[idx].task;
    }

    void Release(T* task)
    {
        const uint32_t idx = SlotIndex(task);
        R_ASSERT2(idx < N, "TaskPool::Release — pointer out of range");

        std::lock_guard<std::mutex> lock(freeMutex);
        meta.nextFree[idx] = meta.freeHead;
        meta.freeHead = idx;
    }

private:
    // Well-defined address arithmetic on the object representation instead of
    // reinterpret_cast<Slot*>(task) - slots (UB unless task really is a Slot).
    uint32_t SlotIndex(T* task) const
    {
        const char*     p    = reinterpret_cast<const char*>(task);
        const char*     base = reinterpret_cast<const char*>(slots);
        const ptrdiff_t diff = p - base;
        R_ASSERT2(diff % sizeof(Slot) == 0, "TaskPool::Release — misaligned slot pointer");
        return static_cast<uint32_t>(diff / sizeof(Slot));
    }
};

struct SimpleTask;
struct RangeTask;
struct FreeSlotTask;

// Depth counter for IsInsideTask(): incremented while a task's callback runs.
// Declared before the task structs so inline ExecuteRange bodies can see it.
static thread_local uint32_t g_taskDepth = 0;

// Task objects serve two purposes: they are enki::ITaskSet implementations
// (executed by the scheduler) and intrusive recycle-queue nodes
// (RecyclableTask), published by themselves once their ExecuteRange has run.
// Multiple inheritance keeps the two roles independent - AddTaskSetToPipe
// up-casts to ITaskSet*, PushRecycle up-casts to RecyclableTask*; no field
// aliasing/unions needed (m_SetSize is a separate member here anyway, on the
// private ITaskSet side of an unrelated object layout).
struct SimpleTask : public enki::ITaskSet, public RecyclableTask
{
    CTaskManager::TaskFunc func = nullptr;
    bool                   heapAllocated = false;

    SimpleTask() { m_SetSize = 1; }

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
    void Recycle() override;
};

struct FreeSlotTask : public enki::ITaskSet, public RecyclableTask
{
    // The RangeTask this slot gates. It is NOT pushed into the recycle queue
    // itself; FreeSlotTask::Recycle reclaims it AFTER this task, so the
    // dependency node can be cleared while the owner is still alive.
    RangeTask* ownerToRecycleAfter = nullptr;
    bool       heapAllocated = false;
    enki::Dependency dep;

    FreeSlotTask() { m_SetSize = 1; }
    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override;
    void Recycle() override;
};

struct RangeTask : public enki::ITaskSet, public RecyclableTask
{
    CTaskManager::TaskRangeFunc func = nullptr;
    bool                        heapAllocated = false;

    RangeTask() = default;

    void ExecuteRange(enki::TaskSetPartition range, uint32_t threadnum) override
    {
        ++g_taskDepth;
        if (func) func(range.start, range.end, threadnum);
        --g_taskDepth;
    }
    void Recycle() override;
};

static FreeListPool<SimpleTask, SIMPLE_POOL_SIZE>       g_SimplePool;
static FreeListPool<RangeTask, RANGE_POOL_SIZE>         g_RangePool;
static FreeListPool<FreeSlotTask, FREE_SLOT_POOL_SIZE>  g_FreeSlotPool;

// Recycle()/ExecuteRange() bodies live here, after the pools they refer to.
// Recycle() (cf. FreeSlotTask/SimpleTask/CallbackTask) is called from
// FlushRecycledTasks() at WaitAll()/Destroy(), i.e. after the task is fully
// complete.

void SimpleTask::ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/)
{
    ++g_taskDepth;
    if (func) func();
    --g_taskDepth;
    // The scheduler still touches this object after ExecuteRange returns (the
    // final m_RunningCount.fetch_sub and possibly TaskComplete), so never
    // reclaim here - publish the node and let WaitAll()/Destroy() do it.
    PushRecycle(this);
}

void SimpleTask::Recycle()
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

void FreeSlotTask::ExecuteRange(enki::TaskSetPartition /*range*/, uint32_t /*threadnum*/)
{
    // Nothing to compute; this task exists only to run after its owner.
    PushRecycle(this);
}

void FreeSlotTask::Recycle()
{
    // One node, one call preserves the required order: clear the dependency
    // node while the owner RangeTask is still alive, then recycle this task,
    // then its owner. Deleting/pooling the owner first would leave dep
    // pointing at freed memory - ClearDependency walks the owner's
    // m_pDependents list. Runs at WaitAll()/Destroy() where both tasks are
    // complete, so ClearDependency's GetIsComplete() asserts hold.
    RangeTask* owner = ownerToRecycleAfter;
    ownerToRecycleAfter = nullptr;

    dep.ClearDependency();

    if (heapAllocated)
    {
        delete this;
    }
    else
    {
        g_FreeSlotPool.Release(this);
    }

    if (owner)
    {
        owner->Recycle();
    }
}

void RangeTask::Recycle()
{
    if (heapAllocated)
    {
        delete this;
    }
    else
    {
        func = nullptr;
        g_RangePool.Release(this);
    }
}

// CompletionTask/CallbackTask pair for CTaskManager::AddTaskWithCompletion.
// File scope (were function-local) so the intrusive queue node, the layout
// with m_SetSize=1, and the Recycle() overrides are each defined once.

struct CompletionTask : public enki::ITaskSet
{
    CTaskManager::TaskFunc func = nullptr;

    CompletionTask() { m_SetSize = 1; }

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override
    {
        if (func) func();
    }
};

struct CallbackTask : public enki::ITaskSet, public RecyclableTask
{
    CTaskManager::CompletionCallback callback;
    CompletionTask*     pMainToDelete = nullptr; // created with new; deleted in Recycle()
    enki::Dependency   dep;

    CallbackTask() { m_SetSize = 1; }

    void ExecuteRange(enki::TaskSetPartition, uint32_t) override;
    void Recycle() override;
};

void CallbackTask::ExecuteRange(enki::TaskSetPartition, uint32_t)
{
    if (callback) callback();
    PushRecycle(this);
}

void CallbackTask::Recycle()
{
    // Explicit order, mirroring FreeSlotTask::Recycle. Do NOT rely on
    // ICompletable::~ICompletable (TaskScheduler.h:~ICompletable) to null the
    // dep node for us - that node-nulling behaviour is easy to drop across
    // enkiTS versions. Clear the dependency while mainTask is still alive,
    // then delete this CallbackTask, then delete mainTask:
    //   * ~CallbackTask -> ~Dependency -> ClearDependency() is then a no-op
    //     (pDependencyTask already null);
    //   * ~CompletionTask -> ~ICompletable walks mainTask's m_pDependents,
    //     which ClearDependency already emptied.
    CompletionTask* mainToDelete = pMainToDelete;
    pMainToDelete = nullptr;
    dep.ClearDependency();
    delete this;
    delete mainToDelete;
}

void CTaskManager::AddTask(TaskFunc func)
{
    // Batch-entrance guard: queue depth at/past HARD_LIMIT means a previous
    // WaitAll() was skipped (see the contract comment near the limits). Debug:
    // hard fail. Release: one-shot LogWarning so the violation stays visible.
#ifdef DEBUG
    R_ASSERT2(g_RecycleCount.load(std::memory_order_relaxed) < HARD_LIMIT,
        "Recycle queue overflow: WaitAll not called between batches");
#endif
    if (g_RecycleCount.load(std::memory_order_relaxed) >= SOFT_LIMIT &&
        !g_RecycleSoftWarned.exchange(true))
    {
        LogWarning("Recycle queue depth %u exceeded soft limit %u: "
            "likely a missing WaitAll() between task batches",
            g_RecycleCount.load(std::memory_order_relaxed), SOFT_LIMIT);
    }

    // Scheduler must be initialized before submitting (VERIFY is DEBUG-only).
    VERIFY(g_Scheduler.GetIsRunning());
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

    // Guarantee: enkiTS returns m_RunningCount to exactly 0 once a task has run
    // - TaskComplete stores 0 (TaskScheduler.cpp:524) and the guard-credit
    // accounting in SplitAndAddTask/TryRunTask (:707-742) keeps the tally exact;
    // re-use after completion is enkiTS's stated contract (TaskScheduler.h:108
    // "TaskSets can be re-used, but check completion first", asserted in
    // AddTaskSetToPipe, TaskScheduler.cpp:783 - that assert is Debug-only).
    // Our deferred recycle guarantees the slot is only re-acquired once the
    // object is really complete, so this all-build assert is a sanity net.
    R_ASSERT2(task->GetIsComplete(), "AddTask: reusing a task that has not completed");
    task->func = func;
    g_Scheduler.AddTaskSetToPipe(task);
}

void CTaskManager::AddTaskWithCompletion(TaskFunc func, CompletionCallback onComplete)
{
    // Scheduler must be initialized before submitting (VERIFY is DEBUG-only).
    VERIFY(g_Scheduler.GetIsRunning());
    VERIFY(func);

    CompletionTask* mainTask = new CompletionTask();
    mainTask->func = func;

    CallbackTask* cbTask = new CallbackTask();
    cbTask->callback = onComplete;
    cbTask->pMainToDelete = mainTask;
    cbTask->dep.SetDependency(mainTask, cbTask);

    // Both mainTask's ExecuteRange and cbTask's ExecuteRange run once; cbTask
    // cannot start before mainTask completes, so the callback fires exactly
    // once. Both objects are deleted ("recycled") from cbTask's Recycle().
    g_Scheduler.AddTaskSetToPipe(mainTask);
}

bool CTaskManager::IsRunning()
{
    return g_Scheduler.GetIsRunning();
}

bool CTaskManager::IsInsideTask()
{
    return g_taskDepth > 0;
}

void CTaskManager::AddTaskRange(TaskRangeFunc func, uint32_t setSize, uint32_t minRange)
{
    // Batch-entrance guard: same contract check as AddTask (see its comment).
#ifdef DEBUG
    R_ASSERT2(g_RecycleCount.load(std::memory_order_relaxed) < HARD_LIMIT,
        "Recycle queue overflow: WaitAll not called between batches");
#endif
    if (g_RecycleCount.load(std::memory_order_relaxed) >= SOFT_LIMIT &&
        !g_RecycleSoftWarned.exchange(true))
    {
        LogWarning("Recycle queue depth %u exceeded soft limit %u: "
            "likely a missing WaitAll() between task batches",
            g_RecycleCount.load(std::memory_order_relaxed), SOFT_LIMIT);
    }

    // Scheduler must be initialized before submitting (VERIFY is DEBUG-only).
    VERIFY(g_Scheduler.GetIsRunning());
    VERIFY(func);
    VERIFY(setSize > 0);
    // minRange must be >= 1: with minRange == 0, SplitTask/AddTaskSetToPipeInt
    // in enkiTS can compute a zero-sized partition for small set sizes and
    // loop forever (TaskScheduler.cpp:SplitAndAddTask). minRange > setSize is
    // legal - SplitTask clamps to the remaining range, the whole set runs as
    // one partition (xrFS/xrArchiver pass minRange 4 with setSize possibly 1).
    VERIFY(minRange >= 1);

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

    // Guarantee (same invariant as AddTask): a completed task ends with
    // m_RunningCount == 0 (TaskScheduler.cpp:TaskComplete:524; guard credits
    // SplitAndAddTask:707-742; re-use contract TaskScheduler.h:108). The dep is
    // reusable too: ClearDependency (called in FreeSlotTask::Recycle) unlinks
    // from m_pDependents and nulls all three node fields (:1405-1436), and
    // SetDependency re-initializes the node (:1393-1403), asserting both sides
    // complete. These all-build asserts back up enkiTS's Debug-only assert.
    R_ASSERT2(task->GetIsComplete(), "AddTaskRange: reusing a task that has not completed");
    R_ASSERT2(fst->GetIsComplete(), "AddTaskRange: reusing a slot task that has not completed");

    fst->ownerToRecycleAfter = task;
    fst->dep.SetDependency(task, fst);

    g_Scheduler.AddTaskSetToPipe(task);
}

void CTaskManager::WaitAll()
{
    g_Scheduler.WaitforAll();
    // Tasks were only deferred during execution; reclaim them now that all
    // worker threads have stopped touching task objects.
    FlushRecycledTasks();
}

static uint32_t GetPhysicalCoreCount()
{
    // Preferred path: GetLogicalProcessorInformationEx (also covers machines
    // with more than 64 logical processors). The first call with a NULL buffer
    // returns ERROR_INSUFFICIENT_BUFFER together with the required size.
    DWORD bufLen = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufLen) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && bufLen > 0)
    {
        std::vector<uint8_t> buffer(bufLen);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()),
            &bufLen))
        {
            uint32_t physical = 0;
            const uint8_t* p = buffer.data();
            const uint8_t* end = buffer.data() + buffer.size();
            while (p < end)
            {
                const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(p);
                if (info->Relationship == RelationProcessorCore)
                    ++physical;
                if (info->Size == 0)
                    break;
                p += info->Size;
            }
            if (physical > 0)
                return physical;
        }
    }

    // Fallback: the legacy API (limited to 64 logical processors). Same
    // ERROR_INSUFFICIENT_BUFFER protocol as above.
    DWORD legacyLen = 0;
    if (!GetLogicalProcessorInformation(nullptr, &legacyLen) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && legacyLen > 0)
    {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> legacy(
            legacyLen / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(legacy.data(), &legacyLen))
        {
            uint32_t physical = 0;
            for (const auto& info : legacy)
            {
                if (info.Relationship == RelationProcessorCore)
                    ++physical;
            }
            if (physical > 0)
                return physical;
        }
    }

    // Last resort: logical cores are usually 2x physical. hardware_concurrency()
    // may report 0; std::max(1u, ...) guarantees the result is always >= 1.
    const uint32_t logical = std::thread::hardware_concurrency();
    return std::max(1u, logical / 2);
}

void CTaskManager::Initialize(uint32_t workerThreadOverride)
{
    // Contract: call Initialize() before the first Add*/WaitAll, and call
    // Destroy() at shutdown once. A repeated Initialize() without a preceding
    // Destroy() is still safe on the enkiTS side - TaskScheduler::Initialize
    // stops and joins the worker threads first (TaskScheduler.cpp:StopThreads),
    // and WaitforAllAndShutdown/Destroy resets m_bRunning to false, so the
    // sequence Initialize -> Destroy -> Initialize is fully supported.
    // Therefore there is deliberately NO VERIFY(!GetIsRunning()) here - it
    // would false-positive the supported re-initialize path.
    enki::TaskSchedulerConfig config;
    config.customAllocator.alloc = enki::EnkiAllocFunc;
    config.customAllocator.free = enki::EnkiFreeFunc;

    const uint32_t logicalThreads = std::thread::hardware_concurrency();

    if (workerThreadOverride > 0)
    {
        config.numTaskThreadsToCreate = workerThreadOverride;
    }
    else
    {
        config.numTaskThreadsToCreate = (logicalThreads > 2) ? (logicalThreads - 2) : 1;
    }

    g_Scheduler.Initialize(config);

    LogInfo("Initialized. Logical: %u, Physical: %u, Workers: %u, Simple: %u, Range: %u, FreeSlot: %u",
        logicalThreads, GetPhysicalCoreCount(), config.numTaskThreadsToCreate,
        SIMPLE_POOL_SIZE, RANGE_POOL_SIZE, FREE_SLOT_POOL_SIZE);
}

void CTaskManager::Destroy()
{
    g_Scheduler.WaitforAllAndShutdown();
    // No worker thread can touch tasks anymore; reclaim everything deferred.
    FlushRecycledTasks();
    LogInfo("Destroyed.");
}