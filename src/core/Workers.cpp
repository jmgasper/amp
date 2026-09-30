#include "Workers.h"
#include <algorithm>
#include <thread>
#include <vector>

#ifdef __HAIKU__
#include <OS.h>
#endif

namespace amp {

int ProcessorCount()
{
#ifdef __HAIKU__
    system_info info;
    if (get_system_info(&info) == B_OK && info.cpu_count > 0)
        return (int)info.cpu_count;
#endif
    return std::max(1, (int)std::thread::hardware_concurrency());
}

#ifdef __HAIKU__

namespace {

struct Job {
    const std::function<void(int)>* work;
    int index;
};

int32 RunJob(void* data)
{
    Job* job = static_cast<Job*>(data);
    (*job->work)(job->index);
    return 0;
}

} // namespace

void RunWorkers(int count, const char* name, const std::function<void(int)>& work)
{
    count = std::max(1, count);
    std::vector<Job> jobs(count);
    std::vector<thread_id> threads;
    for (int i = 0; i < count; i++) {
        jobs[i] = {&work, i};
        thread_id thread = spawn_thread(RunJob, name, B_LOW_PRIORITY, &jobs[i]);
        if (thread >= 0 && resume_thread(thread) == B_OK)
            threads.push_back(thread);
        else if (thread >= 0)
            kill_thread(thread);
    }
    // no thread to be had: do the work here rather than not at all
    if (threads.empty())
        work(0);
    for (thread_id thread : threads) {
        status_t result;
        while (wait_for_thread(thread, &result) == B_INTERRUPTED) {
        }
    }
}

#else

void RunWorkers(int count, const char* name, const std::function<void(int)>& work)
{
    count = std::max(1, count);
    std::vector<std::thread> threads;
    for (int i = 0; i < count; i++)
        threads.emplace_back([&work, i] { work(i); });
    for (std::thread& thread : threads)
        thread.join();
}

#endif

} // namespace amp
