// Wii U-mode adapters: original 37-byte reports and four independent rumble motors.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gc_adapter.h"
#include <libusb.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace linux_gc {
namespace {
using Clock = std::chrono::steady_clock;
std::thread reader, writer;
std::atomic<bool> running{false};
std::atomic<double> frequency{0};
std::atomic<bool> dirty{false};
std::array<std::atomic<unsigned char>, 4> motors{};
std::mutex report_mutex, output_mutex, wake_mutex;
std::condition_variable wake;
std::array<unsigned char, 37> report{};
Clock::time_point received;
bool have_report = false;
struct Origin { bool set = false; unsigned char sx, sy, cx, cy, tl, tr; } origins[4]{};
libusb_context* context = nullptr;
libusb_device_handle* device = nullptr;
int interface_number = -1;
unsigned char input_endpoint = 0x81, output_endpoint = 0x02;

void clear_report() {
  std::lock_guard<std::mutex> lock(report_mutex);
  have_report = false;
  for (auto& origin : origins) origin.set = false;
  frequency.store(0);
}
bool write_locked(const unsigned char* bytes, int length) {
  if (!device) return false;
  int transferred = 0;
  int result = libusb_interrupt_transfer(device, output_endpoint,
    const_cast<unsigned char*>(bytes), length, &transferred, 32);
  if (result == LIBUSB_ERROR_PIPE) {
    libusb_clear_halt(device, output_endpoint);
    result = libusb_interrupt_transfer(device, output_endpoint,
      const_cast<unsigned char*>(bytes), length, &transferred, 32);
  }
  return result == 0 && transferred == length;
}
void close_device() {
  std::lock_guard<std::mutex> lock(output_mutex);
  if (device) {
    const unsigned char off[] = {0x11, 0, 0, 0, 0};
    write_locked(off, sizeof off);
    if (interface_number >= 0) libusb_release_interface(device, interface_number);
    libusb_close(device);
    device = nullptr;
  }
  interface_number = -1;
  for (auto& motor : motors) motor.store(0);
  clear_report();
}
bool open_device() {
  std::lock_guard<std::mutex> lock(output_mutex);
  libusb_device** devices = nullptr;
  const auto count = libusb_get_device_list(context, &devices);
  if (count < 0) return false;
  int last_error = LIBUSB_ERROR_NO_DEVICE;
  for (ssize_t index = 0; index < count && !device; ++index) {
    libusb_device_descriptor descriptor{};
    if (libusb_get_device_descriptor(devices[index], &descriptor) != 0 ||
        descriptor.idVendor != 0x057e || descriptor.idProduct != 0x0337) continue;
    libusb_config_descriptor* configuration = nullptr;
    if (libusb_get_active_config_descriptor(devices[index], &configuration) != 0) continue;
    int selected_interface = -1, alternate_setting = 0;
    for (int i = 0; i < configuration->bNumInterfaces && selected_interface < 0; ++i) {
      const auto& interface = configuration->interface[i];
      for (int a = 0; a < interface.num_altsetting; ++a) {
        const auto& alternate = interface.altsetting[a];
        unsigned char in = 0, out = 0;
        for (int e = 0; e < alternate.bNumEndpoints; ++e) {
          const auto& endpoint = alternate.endpoint[e];
          if ((endpoint.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
          if (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) in = endpoint.bEndpointAddress;
          else out = endpoint.bEndpointAddress;
        }
        if (in && out) {
          selected_interface = alternate.bInterfaceNumber;
          alternate_setting = alternate.bAlternateSetting;
          input_endpoint = in; output_endpoint = out;
          break;
        }
      }
    }
    libusb_free_config_descriptor(configuration);
    if (selected_interface < 0) continue;
    last_error = libusb_open(devices[index], &device);
    if (last_error < 0) { device = nullptr; continue; }
    interface_number = -1;
    last_error = libusb_set_auto_detach_kernel_driver(device, 1);
    if (last_error >= 0 || last_error == LIBUSB_ERROR_NOT_SUPPORTED)
      last_error = libusb_claim_interface(device, selected_interface);
    if (last_error >= 0) {
      interface_number = selected_interface;
      if (alternate_setting) last_error = libusb_set_interface_alt_setting(device, interface_number, alternate_setting);
    }
    if (last_error >= 0) {
      const unsigned char start = 0x13, off[] = {0x11, 0, 0, 0, 0};
      if (write_locked(&start, 1) && write_locked(off, sizeof off)) {
        std::fprintf(stderr, "GameCube adapter: native Linux USB, interface %d, endpoints %02x/%02x\n",
                     interface_number, input_endpoint, output_endpoint);
        dirty.store(true);
        break;
      }
      last_error = LIBUSB_ERROR_IO;
    }
    if (interface_number >= 0) libusb_release_interface(device, interface_number);
    libusb_close(device); device = nullptr; interface_number = -1;
  }
  libusb_free_device_list(devices, 1);
  static int logged_error = LIBUSB_ERROR_NO_DEVICE;
  if (!device && last_error != logged_error) {
    std::fprintf(stderr, "GameCube adapter: %s (check USB udev permissions and other adapter readers)\n",
                 libusb_error_name(last_error));
    logged_error = last_error;
  }
  return device != nullptr;
}
void read_loop() {
  while (running.load()) {
    if (!device && !open_device()) {
      std::unique_lock<std::mutex> lock(wake_mutex);
      wake.wait_for(lock, std::chrono::seconds(1), [] { return !running.load(); });
      continue;
    }
    auto window = Clock::now(), last_good = window, last_restart = window;
    unsigned samples = 0;
    while (running.load()) {
      unsigned char raw[37];
      int transferred = 0;
      int result = libusb_interrupt_transfer(device, input_endpoint, raw, sizeof raw, &transferred, 100);
      const auto now = Clock::now();
      if (result == 0 && transferred == 37 && raw[0] == 0x21) {
        last_good = now;
        if (++samples >= 50) {
          const double seconds = std::chrono::duration<double>(now - window).count();
          frequency.store(seconds > 0 ? samples / seconds : 0);
          samples = 0; window = now;
        }
        std::lock_guard<std::mutex> lock(report_mutex);
        std::memcpy(report.data(), raw, sizeof raw);
        received = now; have_report = true;
      } else if (result != 0 && result != LIBUSB_ERROR_TIMEOUT) {
        break;
      }
      if (now - last_good >= std::chrono::milliseconds(500)) clear_report();
      if (now - last_good >= std::chrono::seconds(1) && now - last_restart >= std::chrono::seconds(1)) {
        std::lock_guard<std::mutex> lock(output_mutex);
        libusb_clear_halt(device, input_endpoint);
        const unsigned char start = 0x13;
        write_locked(&start, 1);
        last_restart = now; window = now; samples = 0;
      }
    }
    close_device();
  }
  close_device();
}
void write_loop() {
  while (running.load()) {
    std::unique_lock<std::mutex> lock(wake_mutex);
    wake.wait_for(lock, std::chrono::milliseconds(80), [] { return !running.load() || dirty.load(); });
    lock.unlock();
    if (!running.load()) break;
    if (dirty.exchange(false)) {
      std::lock_guard<std::mutex> output_lock(output_mutex);
      if (device) {
        const unsigned char bytes[] = {0x11, motors[0].load(), motors[1].load(), motors[2].load(), motors[3].load()};
        if (!write_locked(bytes, sizeof bytes)) dirty.store(true);
      }
    }
  }
}
}
void initialize() {
  if (running.load()) return;
  if (libusb_init(&context) != 0) { context = nullptr; return; }
  running.store(true);
  reader = std::thread(read_loop);
  writer = std::thread(write_loop);
}
unsigned poll(MuPadStatus out[4]) {
  std::lock_guard<std::mutex> lock(report_mutex);
  if (!have_report || Clock::now() - received >= std::chrono::milliseconds(500)) {
    have_report = false;
    frequency.store(0);
    for (auto& origin : origins) origin.set = false;
    return 0;
  }
  unsigned mask = 0;
  for (int index = 0; index < 4; ++index) {
    const auto* pad = report.data() + 1 + 9 * index;
    auto& origin = origins[index];
    if (!(pad[0] & 0x30)) { origin.set = false; continue; }
    if (!origin.set) origin = {true, pad[3], pad[4], pad[5], pad[6], pad[7], pad[8]};
    auto& state = out[index]; state = {}; state.err = 0;
    state.button = ((pad[1] & 0x0f) << 8) | ((pad[1] & 0x10) ? 1 : 0) | ((pad[1] & 0x20) ? 2 : 0) |
                   ((pad[1] & 0x40) ? 4 : 0) | ((pad[1] & 0x80) ? 8 : 0) |
                   ((pad[2] & 1) ? 0x1000 : 0) | ((pad[2] & 2) ? 0x10 : 0) |
                   ((pad[2] & 4) ? 0x20 : 0) | ((pad[2] & 8) ? 0x40 : 0);
    auto axis = [](unsigned char value, unsigned char rest) -> int8_t {
      const int difference = int(value) - int(rest);
      return int8_t(difference > 127 ? 127 : difference < -128 ? -128 : difference);
    };
    state.stick_x = axis(pad[3], origin.sx); state.stick_y = axis(pad[4], origin.sy);
    state.sub_x = axis(pad[5], origin.cx); state.sub_y = axis(pad[6], origin.cy);
    state.trigger_l = pad[7] > origin.tl ? pad[7] - origin.tl : 0;
    state.trigger_r = pad[8] > origin.tr ? pad[8] - origin.tr : 0;
    mask |= 1u << index;
  }
  return mask;
}
void rumble(int port, bool on) {
  if (port < 0 || port > 3 || !running.load()) return;
  if (motors[port].exchange(on ? 1 : 0) != (on ? 1 : 0)) { dirty.store(true); wake.notify_all(); }
}
double poll_rate() { return frequency.load(); }
void shutdown() {
  running.store(false); wake.notify_all();
  if (reader.joinable()) reader.join();
  if (writer.joinable()) writer.join();
  if (context) { libusb_exit(context); context = nullptr; }
}
}
