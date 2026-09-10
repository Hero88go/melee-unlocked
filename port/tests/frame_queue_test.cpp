#include "frame_queue.h"
#include <cstdio>
#include <cstdlib>
#include <future>
static void check(bool ok) { if (!ok) std::abort(); }
static gx::Frame frame(uint64_t sequence) { gx::Frame f; f.sequence = sequence; return f; }
int main() {
  gx::FrameQueue queue;
  check(queue.push(frame(1))); check(queue.push(frame(2)));
  auto blocked = std::async(std::launch::async, [&] { return queue.push(frame(3)); });
  check(blocked.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  gx::Frame out;
  check(queue.pop(out) && out.sequence == 1);
  check(blocked.get());
  queue.finish();
  check(!queue.push(frame(4)));
  check(queue.pop(out) && out.sequence == 2);
  check(queue.pop(out) && out.sequence == 3);
  check(queue.drained() && !queue.pop(out));
  gx::FrameQueue cancelled;
  check(cancelled.push(frame(1))); check(cancelled.push(frame(2)));
  auto waiting = std::async(std::launch::async, [&] { return cancelled.push(frame(3)); });
  cancelled.finish(true);
  check(!waiting.get() && cancelled.drained());
  std::puts("bounded ordering, drain, cancellation and producer wakeup passed");
}
