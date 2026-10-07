// Contain C++ resource failures at optional graphics-driver API boundaries.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <system_error>
namespace gx {
template<class Call, class Result, class Failure>
Result guarded_driver_call(bool& failed, Call call, Result fallback, Failure failure) {
  if (failed) return fallback;
  try { return call(); }
  catch (const std::system_error& error) {
    failed = true;
    failure(error);
    return fallback;
  }
}
}
