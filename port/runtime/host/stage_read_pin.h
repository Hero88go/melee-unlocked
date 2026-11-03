// Keep stage read pieces together without carrying a form across an online-state change.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace host::cosmetics::stage_read_pin {

template <typename Form>
struct PinState {
  Form form{};
  bool online = false;
  std::size_t size = 0;
};

// Form is a nullable handle (including a shared_ptr, so the pinned bytes stay alive).
// A length mismatch serves current without replacing the offset-0 pin in the same state.
template <typename Form>
Form pick(PinState<Form>& pin, const Form& current, bool online_now,
          std::uint32_t offset, std::size_t current_size) {
  if (offset == 0 || !pin.form || pin.online != online_now)
    pin = {current, online_now, current_size};
  return pin.size == current_size ? pin.form : current;
}

}  // namespace host::cosmetics::stage_read_pin
