// A handful of threads doing the same job side by side, for work that waits on the disk or
// the network more than it computes (tag reading, folder walks).
#pragma once
#include <functional>

namespace amp {

// Processors the system offers.
int ProcessorCount();

// Runs `work(index)` on `count` threads named `name` and returns when all of them are done.
// On Haiku the threads come from the Kernel Kit and run below normal priority, so that
// playback and the windows stay ahead of a background job.
void RunWorkers(int count, const char* name, const std::function<void(int)>& work);

} // namespace amp
