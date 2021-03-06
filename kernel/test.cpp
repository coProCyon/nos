#include "trace.h"
#include "debug.h"
#include "task.h"
#include "sched.h"
#include "cpu.h"

#include <lib/btree.h>
#include <lib/error.h>
#include <lib/stdlib.h>
#include <lib/ring_buffer.h>
#include <lib/vector.h>

namespace Kernel
{

Stdlib::Error TestBtree()
{
    Stdlib::Error err;

    Trace(TestLL, "TestBtree: started");

    size_t keyCount = 431;

    Stdlib::Vector<size_t> pos;
    if (!pos.ReserveAndUse(keyCount))
        return MakeError(Stdlib::Error::NoMemory);

    for (size_t i = 0; i < keyCount; i++)
    {
        pos[i] = i;
    }

    Stdlib::Vector<u32> key;
    if (!key.ReserveAndUse(keyCount))
        return MakeError(Stdlib::Error::NoMemory);
    for (size_t i = 0; i < keyCount; i++)
        key[i] = i;

    Stdlib::Vector<u32> value;
    if (!value.ReserveAndUse(keyCount))
        return MakeError(Stdlib::Error::NoMemory);

    for (size_t i = 0; i < keyCount; i++)
        value[i] = i;

    Stdlib::Btree<u32, u32, 4> tree;

    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = 0; i < keyCount; i++)
    {
        if (!tree.Insert(key[pos[i]], value[pos[i]]))
        {
            Trace(TestLL, "TestBtree: cant insert key %llu", key[pos[i]]);
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = 0; i < keyCount; i++)
    {
        bool exist;
        auto foundValue = tree.Lookup(key[pos[i]], exist);
        if (!exist)
        {
            Trace(TestLL, "TestBtree: cant find key");
            return MakeError(Stdlib::Error::Unsuccessful);
        }

        if (foundValue != value[pos[i]])
        {
            Trace(TestLL, "TestBtree: unexpected found value");
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = 0; i < keyCount / 2; i++)
    {
        if (!tree.Delete(key[pos[i]]))
        {
            Trace(TestLL, "TestBtree: cant delete key[%lu][%lu]=%llu", i, pos[i], key[pos[i]]);
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = keyCount / 2; i < keyCount; i++)
    {
        bool exist;
        auto foundValue = tree.Lookup(key[pos[i]], exist);
        if (!exist)
        {
            Trace(TestLL, "TestBtree: cant find key");
            return MakeError(Stdlib::Error::Unsuccessful);
        }

        if (foundValue != value[pos[i]])
        {
            Trace(TestLL, "TestBtree: unexpected found value");
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = keyCount / 2; i < keyCount; i++)
    {
        if (!tree.Delete(key[pos[i]]))
        {
            Trace(TestLL, "TestBtree: cant delete key");
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = 0; i < keyCount; i++)
    {
        bool exist;
        tree.Lookup(key[pos[i]], exist);
        if (exist)
        {
            Trace(TestLL, "TestBtree: key still exist");
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    for (size_t i = 0; i < keyCount; i++)
    {
        if (!tree.Insert(key[pos[i]], value[pos[i]]))
        {
            Trace(TestLL, "TestBtree: can't insert key'");
            return MakeError(Stdlib::Error::Unsuccessful);
        }
    }
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    Trace(TestLL, "TestBtree: min depth %d max depth %d", tree.MinDepth(), tree.MaxDepth());

    tree.Clear();
    if (!tree.Check())
    {
        Trace(TestLL, "TestBtree: check failed");
        return MakeError(Stdlib::Error::Unsuccessful);
    }

    Trace(TestLL, "TestBtree: complete");

    return MakeError(Stdlib::Error::Success);
}

Stdlib::Error TestAllocator()
{
    Stdlib::Error err;

    for (size_t size = 1; size <= 8 * Const::PageSize; size++)
    {
        u8 *block = new u8[size];
        if (block == nullptr)
        {
            return Stdlib::Error::NoMemory;
        }

        block[0] = 1;
        block[size / 2] = 1;
        block[size - 1] = 1;
        delete [] block;
    }

    return MakeError(Stdlib::Error::Success);
}

Stdlib::Error TestRingBuffer()
{
    Stdlib::RingBuffer<u8, 3> rb;

    if (!rb.Put(0x1))
        return MakeError(Stdlib::Error::Unsuccessful);

    if (!rb.Put(0x2))
        return MakeError(Stdlib::Error::Unsuccessful);

    if (!rb.Put(0x3))
        return MakeError(Stdlib::Error::Unsuccessful);

    if (rb.Put(0x4))
        return MakeError(Stdlib::Error::Unsuccessful);

    if (!rb.IsFull())
        return MakeError(Stdlib::Error::Unsuccessful);

    if (rb.IsEmpty())
        return MakeError(Stdlib::Error::Unsuccessful);

    if (rb.Get() != 0x1)
        return MakeError(Stdlib::Error::Unsuccessful);

    if (rb.Get() != 0x2)
        return MakeError(Stdlib::Error::Unsuccessful);

    if (rb.Get() != 0x3)
        return MakeError(Stdlib::Error::Unsuccessful);

    if (!rb.IsEmpty())
        return MakeError(Stdlib::Error::Unsuccessful);
   
    return MakeError(Stdlib::Error::Success);
}

Stdlib::Error Test()
{
    Stdlib::Error err;

    err = TestAllocator();
    if (!err.Ok())
        return err;

    err = TestBtree();
    if (!err.Ok())
        return err;

    err = TestRingBuffer();
    if (!err.Ok())
        return err;

    return err;
}

void TestMultiTaskingTaskFunc(void *ctx)
{
    (void)ctx;

    for (size_t i = 0; i < 2; i++)
    {
        auto& cpu = GetCpu();
        auto task = Task::GetCurrentTask();
        Trace(0, "Hello from task 0x%p pid %u cpu %u", task, task->Pid, cpu.GetIndex());
        Sleep(100 * Const::NanoSecsInMs);
    }
}

bool TestMultiTasking()
{
    Task *task[2] = {0};
    for (size_t i = 0; i < Stdlib::ArraySize(task); i++)
    {
        task[i] = new Task();
        if (task[i] == nullptr)
        {
            for (size_t j = 0; j < i; j++)
            {
                task[j]->Put();
            }
            return false;
        }
    }

    bool result;

    for (size_t i = 0; i < Stdlib::ArraySize(task); i++)
    {
        if (!task[i]->Start(TestMultiTaskingTaskFunc, nullptr))
        {
            for (size_t j = 0; j < i; j++)
            {
                task[j]->Wait();
            }
            result = false;
            goto delTasks;
        }
    }

    for (size_t i = 0; i < Stdlib::ArraySize(task); i++)
    {
        task[i]->Wait();
    }

    result = true;

delTasks:
    for (size_t i = 0; i < Stdlib::ArraySize(task); i++)
    {
        task[i]->Put();
    }

    return result;
}

}
