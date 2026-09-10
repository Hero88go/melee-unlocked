// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <stdexcept>
#include <utility>

namespace melee_unlocked
{
// Host-only timing contract for a future independent rendering consumer.
// This class neither advances emulation nor writes into guest memory.
// Call on one consumer thread; transport snapshots across threads separately.
// Time is monotonic host seconds. A generation changes on rollback, load,
// scene transition, or graphics-resource recreation. The producer must tag
// snapshots BEFORE queueing them, so queued stale work can be rejected.
template <typename Scene> class Timeline
{
public:
  struct Snapshot
  {
    uint64_t generation;
    int64_t frame;
    double time;
    std::shared_ptr<const Scene> scene;
  };
  struct Sample
  {
    std::shared_ptr<const Scene> previous;
    std::shared_ptr<const Scene> current;
    double alpha;
    bool interpolate;
  };

  explicit Timeline(double max_interval = 0.025) : m_max_interval(max_interval)
  {
    if (!std::isfinite(max_interval) || max_interval <= 0)
      throw std::invalid_argument("max_interval must be finite and positive");
  }

  void Invalidate(uint64_t generation)
  {
    if (m_initialized && generation < m_generation)
      return;
    m_generation = generation;
    m_initialized = true;
    m_previous.reset();
    m_current.reset();
  }

  bool Push(Snapshot snapshot)
  {
    if (!snapshot.scene || !std::isfinite(snapshot.time))
      return false;
    if (m_initialized && snapshot.generation < m_generation)
      return false;
    if (!m_initialized || snapshot.generation != m_generation)
      Invalidate(snapshot.generation);
    bool consecutive = false;
    if (m_current)
    {
      // Frame rollback MUST carry a new generation. Refuse out-of-order work.
      if (snapshot.frame <= m_current->frame || snapshot.time <= m_current->time)
        return false;
      consecutive = m_current->frame != std::numeric_limits<int64_t>::max() &&
                    snapshot.frame == m_current->frame + 1 &&
                    snapshot.time - m_current->time <= m_max_interval;
    }
    m_previous = consecutive ? std::move(m_current) : nullptr;
    m_current.reset(new Snapshot(std::move(snapshot)));
    return true;
  }

  Sample At(double time, double delay) const
  {
    if (!std::isfinite(time) || !std::isfinite(delay) || delay < 0)
      throw std::invalid_argument("time and delay must be finite; delay nonnegative");
    if (!m_current)
      return {nullptr, nullptr, 1.0, false};
    if (!m_previous)
      return {m_current->scene, m_current->scene, 1.0, false};
    const double alpha = std::max(0.0, std::min(1.0,
        (time - delay - m_previous->time) / (m_current->time - m_previous->time)));
    return {m_previous->scene, m_current->scene, alpha, alpha > 0.0 && alpha < 1.0};
  }

private:
  double m_max_interval;
  bool m_initialized = false;
  uint64_t m_generation = 0;
  std::unique_ptr<Snapshot> m_previous;
  std::unique_ptr<Snapshot> m_current;
};

// A display clock independent of emulated frames. Zero means uncapped.
// Missed deadlines are dropped, never replayed as a burst of stale presents.
class PresentClock
{
public:
  explicit PresentClock(double fps) : m_period(fps == 0 ? 0 : 1.0 / fps)
  {
    if (!std::isfinite(fps) || fps < 0 || !std::isfinite(m_period))
      throw std::invalid_argument("fps must be finite and nonnegative");
  }
  bool Due(double now)
  {
    if (!std::isfinite(now))
      return false;
    if (!m_started || now < m_last)
    {
      m_started = true;
      m_next = now;
    }
    m_last = now;
    if (now < m_next)
      return false;
    if (m_period > 0)
      m_next += (std::floor((now - m_next) / m_period) + 1) * m_period;
    else
      m_next = now;
    return true;
  }
private:
  double m_period;
  double m_next = 0;
  double m_last = 0;
  bool m_started = false;
};
} // namespace melee_unlocked
