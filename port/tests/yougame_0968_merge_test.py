"""Source regressions for the v0.9.68 presentation and stage-loader merge.

Windows builds and runtime checks run in the later release stages. Existing
listing, policy, results, focus and agent-hook regressions remain unchanged.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BACKENDS = ("gx_d3d11.cpp", "gx_d3d12.cpp")


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class YouGame0968MergeTest(unittest.TestCase):
    def test_presentation_boundaries_have_no_conflict_markers(self):
        for name in BACKENDS:
            with self.subTest(backend=name):
                self.assertIsNone(re.search(r"^(?:<{7}|={7}|>{7})(?: |$)",
                                            source("port/runtime/gx/" + name), re.M))

    def test_present_uses_actual_fullscreen_state_inside_wait_scope(self):
        # A refused exclusive request leaves the preference enabled. Present must
        # use the driver's actual state and still account its blocking time as wait.
        for name in BACKENDS:
            with self.subTest(backend=name):
                backend = source("port/runtime/gx/" + name)
                # Melee Unlocked 1.0: a hidden window only tests (DXGI_PRESENT_TEST), the other call
                # presents. Both sit inside the one wait scope.
                self.assertEqual(backend.count("swapchain_->Present("), 2)
                present = re.search(
                    r"const HRESULT present_result = \[&\] \{(.*?)\}\(\);",
                    backend, re.S)
                self.assertIsNotNone(present)
                wait = "host::perf::Scope wait_cost(host::perf::Stage::Wait);"
                self.assertLess(present.group(1).index(wait),
                                present.group(1).index("swapchain_->Present("))
                self.assertIn("swapchain_->Present(0, DXGI_PRESENT_TEST)", present.group(1))
                extra = "allow_tearing_ && " if name == "gx_d3d11.cpp" else ""
                self.assertIn(
                    ": swapchain_->Present(synced ? 1 : 0, (!synced && " + extra +
                    "!fullscreen_state_.exclusive_active) ? DXGI_PRESENT_ALLOW_TEARING : 0);",
                    present.group(1))
                self.assertNotIn("opts_.exclusive_fullscreen", present.group(1))

    def test_ready_observes_successful_real_frames_including_occlusion(self):
        # DXGI_STATUS_OCCLUDED is a success code. The app hides the game until
        # ready, so requiring visibility or S_OK would leave its cover up forever.
        for name in BACKENDS:
            with self.subTest(backend=name):
                backend = source("port/runtime/gx/" + name)
                signal = "yougame::parity::signals.presented(SUCCEEDED(present_result) && frame.test_fill < 0);"
                self.assertEqual(backend.count(signal), 1)
                present = backend.index("const HRESULT present_result =")
                ready = backend.index(signal)
                self.assertLess(present, ready)
                elapsed = re.search(r"present_wait_ \+?= Stopwatch::now\(\) - present_call;",
                                    backend[ready:])
                self.assertIsNotNone(elapsed)

    def test_new_stage_forms_keep_online_state_in_the_read_pin(self):
        # v0.9.68 changes stage grafts and invalidates their cached verdicts.
        # The new forms must still pass through loader #9's state-aware pin.
        cosmetics = source("port/runtime/host/cosmetic_mods.cpp")
        read = cosmetics.split("OverrideRead read(", 1)[1].split("std::atomic<OnlineProbe>", 1)[0]
        selection = read.index("found->second.online_bytes && online_now ?")
        pin = read.index("stage_read_pin::pick(pin, form, online_now, file_offset, form->size())")
        self.assertLess(selection, pin)
        self.assertLess(pin, read.index("const auto& bytes = *form;"))
        self.assertIn('found->second.kind == "stage_visual" || found->second.kind == kStageLengthKind',
                      read[selection:pin])
        helper = source("port/runtime/host/stage_read_pin.h")
        self.assertIn("offset == 0 || !pin.form || pin.online != online_now", helper)
        self.assertIn("pin = {current, online_now, current_size};", helper)


if __name__ == "__main__":
    unittest.main()
