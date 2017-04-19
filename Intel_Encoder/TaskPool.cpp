#include "stdafx.h"
#include <cassert>

#include "TaskPool.h"


TaskPool::TaskPool() : first(0)
{
}


TaskPool::~TaskPool()
{
}

void TaskPool::CreateTasks(int num, size_t size)
{
    tasks.clear();
    tasks.resize(num);
    for (int i = 0; i < tasks.size(); i++)
    {
        tasks[i].bitstream.Data = new mfxU8[size];
        tasks[i].bitstream.MaxLength = size;
    }    
}

int TaskPool::GetFreeIndex()
{
    for (int i = 0; i < tasks.size(); i++)
    {
        if (!tasks[i].sync_point)
            return i;
    }
    return -1;
}

Task& TaskPool::GetTask(int ind)
{
    return tasks[ind];
}

int TaskPool::GetFirstIndex()
{
    return first;
}

Task& TaskPool::GetFirstTask()
{
    return tasks[first];
}

void TaskPool::IncrementFirst()
{
    first = (first + 1) % tasks.size();
}
