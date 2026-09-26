#include "frame_queue.h"
#include <cstdio>
#include <cstdlib>
#include <future>
#define check(x) do { if (!(x)) { std::printf("frame_queue_test FAILED line %d: %s\n", __LINE__, #x); std::fflush(stdout); std::abort(); } } while (0)
static gx::Frame frame(uint64_t sequence) { gx::Frame f; f.sequence = sequence; return f; }
int main() {
  // The queue used to block the producer at two frames, which made the renderer able to stall the
  // 60 Hz simulation: a slow presented frame cost a simulation tick and the game ran in slow
  // motion. Frames are never dropped (EFB copies made in one frame feed later ones), so instead the
  // renderer drains a backlog without presenting and the cap exists only to catch a renderer that
  // has stopped consuming altogether. This test pins that contract: pushing a backlog must not
  // block, and order must survive it.
  {
    gx::FrameQueue queue;
    for (uint64_t i = 1; i <= 31; ++i) {
      auto pushed = std::async(std::launch::async, [&] { return queue.push(frame(i)); });
      check(pushed.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready);
      check(pushed.get());
    }
    check(queue.size() == 31);
    gx::Frame out;
    for (uint64_t i = 1; i <= 31; ++i) { check(queue.pop(out)); check(out.sequence == i); }
    check(!queue.pop(out));
  }
  // At the cap the producer waits rather than dropping a frame, and a pop releases it.
  {
    gx::FrameQueue queue;
    for (uint64_t i = 1; i <= 32; ++i) check(queue.push(frame(i)));
    auto blocked = std::async(std::launch::async, [&] { return queue.push(frame(33)); });
    check(blocked.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    gx::Frame out;
    check(queue.pop(out) && out.sequence == 1);
    check(blocked.get());
    check(queue.size() == 32);
  }
  // finish() stops new frames but still hands over what is already queued, in order.
  {
    gx::FrameQueue queue;
    check(queue.push(frame(1))); check(queue.push(frame(2)));
    queue.finish();
    check(!queue.push(frame(3)));
    gx::Frame out;
    check(queue.pop(out) && out.sequence == 1);
    check(queue.pop(out) && out.sequence == 2);
    check(queue.drained() && !queue.pop(out));
  }
  // finish(true) discards the backlog and wakes a producer waiting at the cap, so shutdown cannot
  // hang on a renderer that has already gone away.
  {
    gx::FrameQueue cancelled;
    for (uint64_t i = 1; i <= 32; ++i) check(cancelled.push(frame(i)));
    auto waiting = std::async(std::launch::async, [&] { return cancelled.push(frame(33)); });
    check(waiting.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    cancelled.finish(true);
    check(!waiting.get() && cancelled.drained());
  }
  // push_and_recycle hands the producer back a cleared frame, reusing a returned buffer when the
  // renderer has given one back, so the simulation thread does not reallocate every frame.
  {
    gx::FrameQueue queue;
    gx::Frame producer = frame(1);
    check(queue.push_and_recycle(producer));
    check(producer.sequence == 0);            // handed back cleared, ready to refill
    check(producer.vertices.capacity() >= 65536);
    check(producer.draws.capacity() >= 1024);
    gx::Frame out;
    check(queue.pop(out) && out.sequence == 1);
    queue.recycle(std::move(out));
    producer = frame(2);
    check(queue.push_and_recycle(producer));
    check(producer.sequence == 0);
    check(queue.pop(out) && out.sequence == 2);
  }
  // A producer buffer is prepared with headroom from the submitted frame. This keeps an abrupt
  // increase in stage geometry from reallocating and moving live DrawCall snapshots next frame.
  {
    gx::FrameQueue queue;
    gx::Frame producer = frame(1);
    producer.vertices.resize(66000);
    producer.draws.resize(1030);
    check(queue.push_and_recycle(producer));
    check(producer.vertices.capacity() >= 82501);
    check(producer.draws.capacity() >= 1288);
  }
  // try_pop never blocks; wait_available reports a frame arriving rather than spinning.
  {
    gx::FrameQueue queue;
    gx::Frame out;
    check(!queue.try_pop(out));
    check(!queue.wait_available(std::chrono::milliseconds(5)));
    check(queue.push(frame(7)));
    check(queue.wait_available(std::chrono::milliseconds(5)));
    check(queue.try_pop(out) && out.sequence == 7);
  }
  std::puts("backlog without stalling the simulation, ordering, cap, drain, cancellation, recycling passed");
}
