#pragma once
#include <vector>

#include "mfxvideo++.h"

struct Task;

class TaskPool
{
private:
    

public:
    TaskPool();
    ~TaskPool();

    void CreateTasks(int num, size_t size);
    int GetFreeIndex();
    Task& GetTask(int ind);
    int GetFirstIndex();
    Task& GetFirstTask();
    void IncrementFirst();

private:

    std::vector<Task> tasks;
    int first;
};

struct Task
{
    mfxBitstream bitstream;
    mfxSyncPoint sync_point;

    Task() : bitstream{ 0 }, sync_point{ 0 } {}
    Task(size_t bsize) : bitstream{ 0 }, sync_point{ 0 } { bitstream.Data = new mfxU8[bsize]; }
    ~Task() { delete bitstream.Data; }

    Task(const Task&) = delete;
    Task(Task&& task) : bitstream(task.bitstream), sync_point(task.sync_point) { task.bitstream.Data = nullptr; }
};

