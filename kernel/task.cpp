#include "task.h"
#include "trace.h"
#include "asm.h"
#include "sched.h"
#include "cpu.h"
#include "sched.h"
#include "preempt.h"

namespace Kernel
{

Task::Task()
    : TaskQueue(nullptr)
    , Rsp(0)
    , State(0)
    , Flags(0)
    , Prev(nullptr)
    , Magic(TaskMagic)
    , CpuAffinity(~((ulong)0))
    , Pid(InvalidObjectId)
    , Stack(nullptr)
    , Function(nullptr)
    , Ctx(nullptr)
{
    RefCounter.Set(1);
    ListEntry.Init();
    Name[0] = '\0';
}

Task::Task(const char* fmt, ...)
    : Task()
{
    va_list args;
    va_start(args, fmt);
    Stdlib::VsnPrintf(Name, Stdlib::ArraySize(Name), fmt, args);
    va_end(args);
}

Task::~Task()
{
    BugOn(TaskQueue != nullptr);
    BugOn(Stack != nullptr);
}

void Task::Release()
{
    BugOn(TaskQueue != nullptr);

    if (Stack != nullptr)
    {
        delete Stack;
        Stack = nullptr;
    }
}

void Task::Get()
{
    BugOn(RefCounter.Get() <= 0);
    RefCounter.Inc();
}

void Task::Put()
{
    BugOn(RefCounter.Get() <= 0);
    if (RefCounter.DecAndTest())
    {
        Release();
        delete this;
    }
}

void Task::SetStopping()
{
    Flags.SetBit(FlagStoppingBit);
}

bool Task::IsStopping()
{
    return Flags.TestBit(FlagStoppingBit);
}

void Task::Exit()
{
    BugOn(this != GetCurrentTask());

    State.Set(StateExited);
    ExitTime = GetBootTime();
    TaskTable::GetInstance().Remove(this);

    Schedule();

    Panic("Can't be here");
}

void Task::ExecCallback()
{
    BugOn(this != GetCurrentTask());
    StartTime = GetBootTime();
    Function(Ctx);
    Exit();
}

void Task::Exec(void *task)
{
    static_cast<Task *>(task)->ExecCallback();
}

void Task::Wait()
{
    while (State.Get() != StateExited)
    {
        Sleep(1 * Const::NanoSecsInMs);
    }
}

bool Task::PrepareStart(Func func, void* ctx)
{
    BugOn(Stack != nullptr);
    BugOn(Function != nullptr);

    Stack = new struct Stack(this);
    if (Stack == nullptr)
    {
        return false;
    }

    if (!TaskTable::GetInstance().Insert(this))
    {
        delete Stack;
        Stack = nullptr;
        return false;
    }

    Function = func;
    Ctx = ctx;

    return true;
}

bool Task::Start(Func func, void* ctx)
{
    if (!PrepareStart(func, ctx))
        return false;

    ulong* rsp = (ulong *)&Stack->StackTop[0];
    *(--rsp) = (ulong)&Task::Exec;//setup return address
    Context* regs = (Context*)((ulong)rsp - sizeof(*regs));
    Stdlib::MemSet(regs, 0, sizeof(*regs));
    regs->Rdi = (ulong)this; //setup 1arg for Task::Exec
    regs->Rflags = (1 << 9); //setup IF
    Rsp = (ulong)regs;

    StartTime = GetBootTime();
    State.Set(StateWaiting);

    auto taskQueue = SelectNextTaskQueue();
    taskQueue->Insert(this);
    return true;
}

bool Task::Run(class TaskQueue& taskQueue, Func func, void* ctx)
{
    if (!PrepareStart(func, ctx))
        return false;

    SetRsp((ulong)&Stack->StackTop[0]);

    StartTime = GetBootTime();
    RunStartTime = GetBootTime();
    State.Set(StateRunning);

    taskQueue.Insert(this);

    Function(Ctx);

    ExitTime = GetBootTime();

    taskQueue.Remove(this);
    TaskTable::GetInstance().Remove(this);

    return true;
}

Task* Task::GetCurrentTask()
{
    ulong rsp = GetRsp();
    struct Stack* stack = reinterpret_cast<struct Stack *>(rsp & (~(StackSize - 1)));
    if (BugOn(stack->Magic1 != StackMagic1))
        return nullptr;

    if (BugOn(stack->Magic2 != StackMagic2))
        return nullptr;

    if (BugOn(rsp < ((ulong)&stack->StackBottom[0] + Const::PageSize)))
        return nullptr;

    if (BugOn(rsp > (ulong)&stack->StackTop[0]))
        return nullptr;

    Task* task = stack->Task;
    if (BugOn(task->Magic != TaskMagic))
        return nullptr;

    return task;
}

void Task::SetName(const char *fmt, ...)
{
    Name[0] = '\0';

    va_list args;
    va_start(args, fmt);
    Stdlib::VsnPrintf(Name, Stdlib::ArraySize(Name), fmt, args);
    va_end(args);
}

const char* Task::GetName()
{
    return Name;
}

void Task::UpdateRuntime()
{
    auto now = GetBootTime();
    Runtime += (now - RunStartTime);
    RunStartTime = now;
}

void Task::SetCpuAffinity(ulong affinity)
{
    Stdlib::AutoLock lock(Lock);
    CpuAffinity = affinity;
}

ulong Task::GetCpuAffinity()
{
    Stdlib::AutoLock lock(Lock);
    return CpuAffinity;
}

TaskQueue* Task::SelectNextTaskQueue()
{
    class TaskQueue* taskQueue = nullptr;
    ulong cpuMask = CpuTable::GetInstance().GetRunningCpus() & CpuAffinity;
    if (cpuMask != 0)
    {
        for (ulong i = 0; i < 8 * sizeof(ulong); i++)
        {
            if (cpuMask & ((ulong)1 << i))
            {
                auto& candTaskQueue = CpuTable::GetInstance().GetCpu(i).GetTaskQueue();
                if (&candTaskQueue == TaskQueue)
                    continue;

                if (taskQueue == nullptr)
                {
                    taskQueue = &candTaskQueue;
                }
                else
                {
                    if (taskQueue->GetSwitchContextCounter() > candTaskQueue.GetSwitchContextCounter())
                    {
                        taskQueue = &candTaskQueue;
                    }
                }

                continue;
            }
        }
    }

    return taskQueue;
}

TaskTable::TaskTable()
{
}

TaskTable::~TaskTable()
{
    for (size_t i = 0; i < Stdlib::ArraySize(TaskList); i++)
    {
        Stdlib::ListEntry taskList;

        {
            Stdlib::AutoLock lock(Lock[i]);
            taskList.MoveTailList(&TaskList[i]);
        }

        while (!taskList.IsEmpty())
        {
            Task* task = CONTAINING_RECORD(taskList.RemoveHead(), Task, TableListEntry);
            task->TableListEntry.Init();
            task->Put();
        }
    }
}

bool TaskTable::Insert(Task *task)
{
    ObjectId pid = TaskObjectTable.Insert(task);
    if (pid == InvalidObjectId)
    {
        return false;
    }
    task->Pid = pid;

    task->Get();
    size_t i = Stdlib::HashPtr(task) % Stdlib::ArraySize(TaskList);
    Stdlib::AutoLock lock(Lock[i]);

    BugOn(!task->TableListEntry.IsEmpty());
    TaskList[i].InsertTail(&task->TableListEntry);

    return true;
}

void TaskTable::Remove(Task *task)
{
    {
        size_t i = Stdlib::HashPtr(task) % Stdlib::ArraySize(TaskList);

        TaskObjectTable.Remove(task->Pid);

        Stdlib::AutoLock lock(Lock[i]);

        BugOn(task->TableListEntry.IsEmpty());
        task->TableListEntry.RemoveInit();
    }

    task->Put();
}

Task* TaskTable::Lookup(ulong pid)
{
    return reinterpret_cast<Task*>(TaskObjectTable.Lookup(pid));
}

void TaskTable::Ps(Stdlib::Printer& printer)
{
    printer.Printf("pid state flags runtime ctxswitches name\n");

    for (size_t i = 0; i < Stdlib::ArraySize(TaskList); i++)
    {
        Stdlib::AutoLock lock(Lock[i]);

        for (auto currEntry = TaskList[i].Flink;
            currEntry != &TaskList[i];
            currEntry = currEntry->Flink)
        {
            Task* task = CONTAINING_RECORD(currEntry, Task, TableListEntry);
            printer.Printf("%u %u 0x%p %u.%u %u %s\n",
                task->Pid, task->State.Get(), task->Flags.Get(), task->Runtime.GetSecs(),
                task->Runtime.GetUsecs(), task->ContextSwitches.Get(), task->GetName());
        }
    }
}

}