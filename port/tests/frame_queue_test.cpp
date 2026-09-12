#include "frame_queue.h"
#include "drain_policy.h"
#include <cstdio>
#include <cstdlib>
#include <future>
static void check(bool ok) { if (!ok) std::abort(); }
static gx::Frame frame(uint64_t sequence) { gx::Frame f; f.sequence = sequence; return f; }
int main() {
  gx::DrainPolicy policy;
  unsigned drains = 0, presents = 0;
  for (unsigned n=0; n<300; ++n) {
    if (policy.drain(32)) check(++drains <= gx::DrainPolicy::maximum_consecutive);
    else { ++presents; drains = 0; }
  }
  check(presents == 100 && !policy.drain(2));
  gx::FrameQueue queue;
  for (size_t n=1; n<=gx::FrameQueue::capacity; ++n) check(queue.push(frame(n)));
  auto blocked = std::async(std::launch::async, [&] { return queue.push(frame(gx::FrameQueue::capacity+1)); });
  check(blocked.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  gx::Frame out;
  check(queue.pop(out) && out.sequence == 1);
  check(blocked.get());
  queue.finish();
  check(!queue.push(frame(4)));
  for (size_t n=2; n<=gx::FrameQueue::capacity+1; ++n) check(queue.pop(out) && out.sequence == n);
  check(queue.drained() && !queue.pop(out));
  gx::FrameQueue cancelled;
  for (size_t n=1; n<=gx::FrameQueue::capacity; ++n) check(cancelled.push(frame(n)));
  auto waiting = std::async(std::launch::async, [&] { return cancelled.push(frame(gx::FrameQueue::capacity+1)); });
  check(waiting.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
  cancelled.finish(true);
  check(!waiting.get() && cancelled.drained());
  gx::FrameQueue recycling;
  gx::Frame buffer; buffer.vertices.resize(1024);
  auto* storage = buffer.vertices.data();
  recycling.recycle(std::move(buffer));
  gx::Frame next = frame(99);
  check(recycling.push_and_recycle(next));
  check(next.vertices.empty() && next.vertices.capacity() >= 1024 && next.vertices.data() == storage);
  check(recycling.pop(out) && out.sequence == 99);
  std::puts("bounded ordering, drain, cancellation and producer wakeup passed");
}
