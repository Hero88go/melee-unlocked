// Linux build: HID report descriptors parsed into the same capabilities Windows' HID parser gives
// (HidP_*), Raw Input device queries over /dev/hidraw, and the reader thread that hands each report
// to the controller readers in Windows' layout. SPDX-License-Identifier: GPL-2.0-or-later
#include "hidsdi.h"
#include <linux/hidraw.h>
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
// ---- the parsed descriptor: a flat, copyable blob, as RIDI_PREPARSEDDATA hands out on Windows ----
constexpr uint32_t kMagic = 0x4D554844;   // "MUHD"
constexpr int kMaxFields = 192;
constexpr int kMaxUsages = 16;
struct Field {
  uint8_t type;          // HIDP_REPORT_TYPE
  uint8_t report_id;
  uint8_t is_array, is_range, is_absolute, has_null;
  uint16_t bit_offset;   // after the report ID byte
  uint16_t report_size, report_count;
  uint16_t usage_page;
  uint16_t usage_min, usage_max;
  uint16_t usage_count;
  uint16_t usages[kMaxUsages];
  uint16_t collection;   // link collection index
  int32_t logical_min, logical_max, physical_min, physical_max;
};
struct Parsed {
  uint32_t magic;
  uint16_t usage, usage_page;
  uint16_t input_bytes, output_bytes, feature_bytes;
  uint16_t collections;
  uint16_t field_count;
  uint8_t numbered;      // the device uses report IDs
  Field fields[kMaxFields];
};

int32_t sign_extend(uint32_t v, int bytes) {
  if (bytes == 1) return (int8_t)v;
  if (bytes == 2) return (int16_t)v;
  return (int32_t)v;
}

bool parse_descriptor(const uint8_t* d, size_t n, Parsed& out) {
  std::memset(&out, 0, sizeof out);
  out.magic = kMagic;
  struct Globals { uint16_t page = 0; int32_t lmin = 0, lmax = 0, pmin = 0, pmax = 0; uint16_t size = 0, count = 0; uint8_t id = 0; };
  Globals g; std::vector<Globals> stack;
  std::vector<uint16_t> usages; uint32_t umin = 0, umax = 0; bool have_min = false, have_max = false;
  uint32_t bits[3][256] = {};   // per type, per report ID, bits used
  uint16_t collection = 0, depth = 0;
  bool first_usage_seen = false;
  auto clear_locals = [&] { usages.clear(); have_min = have_max = false; umin = umax = 0; };
  for (size_t i = 0; i < n;) {
    const uint8_t prefix = d[i];
    if (prefix == 0xFE) {   // long item
      if (i + 2 >= n) break;
      i += 3 + d[i + 1];
      continue;
    }
    int size = prefix & 3; if (size == 3) size = 4;
    const int type = (prefix >> 2) & 3, tag = prefix >> 4;
    if (i + 1 + size > n) return false;
    uint32_t value = 0;
    for (int k = 0; k < size; ++k) value |= (uint32_t)d[i + 1 + k] << (8 * k);
    i += 1 + size;
    if (type == 1) {   // global
      switch (tag) {
        case 0: g.page = (uint16_t)value; break;
        case 1: g.lmin = sign_extend(value, size); break;
        case 2: g.lmax = size < 4 && g.lmin >= 0 ? (int32_t)value : sign_extend(value, size); break;
        case 3: g.pmin = sign_extend(value, size); break;
        case 4: g.pmax = sign_extend(value, size); break;
        case 7: g.size = (uint16_t)value; break;
        case 8: g.id = (uint8_t)value; out.numbered = 1; break;
        case 9: g.count = (uint16_t)value; break;
        case 10: stack.push_back(g); break;
        case 11: if (!stack.empty()) { g = stack.back(); stack.pop_back(); } break;
        default: break;
      }
    } else if (type == 2) {   // local
      // A 4-byte usage carries its own page in the high half.
      const uint16_t u = (uint16_t)value;
      switch (tag) {
        case 0: usages.push_back(u); if (!first_usage_seen && depth == 0) { out.usage = u; out.usage_page = g.page; first_usage_seen = true; } break;
        case 1: umin = u; have_min = true; break;
        case 2: umax = u; have_max = true; break;
        default: break;
      }
    } else if (type == 0) {   // main
      if (tag == 10) {   // collection
        if (depth == 0 && !first_usage_seen && !usages.empty()) { out.usage = usages[0]; out.usage_page = g.page; first_usage_seen = true; }
        ++depth; collection = out.collections++;
        clear_locals();
      } else if (tag == 12) {   // end collection
        if (depth) --depth;
        clear_locals();
      } else if (tag == 8 || tag == 9 || tag == 11) {   // input, output, feature
        const uint8_t rtype = tag == 8 ? HidP_Input : tag == 9 ? HidP_Output : HidP_Feature;
        const bool constant = value & 1, array = !(value & 2), relative = value & 4, null_state = value & 0x40;
        const uint32_t total = (uint32_t)g.size * g.count;
        uint32_t& used = bits[rtype][g.id];
        if (!constant && out.field_count < kMaxFields && g.size) {
          Field& f = out.fields[out.field_count];
          std::memset(&f, 0, sizeof f);
          f.type = rtype; f.report_id = g.id; f.is_array = array; f.is_absolute = !relative; f.has_null = null_state;
          f.bit_offset = (uint16_t)used; f.report_size = g.size; f.report_count = g.count; f.usage_page = g.page;
          f.collection = collection; f.logical_min = g.lmin; f.logical_max = g.lmax; f.physical_min = g.pmin; f.physical_max = g.pmax;
          if (have_min && have_max) { f.is_range = 1; f.usage_min = (uint16_t)umin; f.usage_max = (uint16_t)umax; }
          else {
            f.usage_count = (uint16_t)std::min<size_t>(usages.size(), kMaxUsages);
            for (int k = 0; k < f.usage_count; ++k) f.usages[k] = usages[k];
            if (f.usage_count) { f.usage_min = f.usages[0]; f.usage_max = f.usages[f.usage_count - 1]; }
          }
          ++out.field_count;
        }
        used += total;
        clear_locals();
      }
    }
  }
  for (int t = 0; t < 3; ++t) {
    uint32_t most = 0;
    for (int id = 0; id < 256; ++id) most = std::max(most, bits[t][id]);
    const uint16_t bytes = most ? (uint16_t)(1 + (most + 7) / 8) : 0;   // + the report ID byte, as Windows counts
    if (t == HidP_Input) out.input_bytes = bytes; else if (t == HidP_Output) out.output_bytes = bytes; else out.feature_bytes = bytes;
  }
  if (!out.collections) out.collections = 1;
  return true;
}

const Parsed* parsed_of(PHIDP_PREPARSED_DATA data) {
  const auto* p = reinterpret_cast<const Parsed*>(data);
  return p && p->magic == kMagic ? p : nullptr;
}

// A field is a button the way Windows' parser decides: one bit per usage, or an array of indices.
bool is_button(const Field& f) { return f.is_array || f.report_size == 1; }

uint32_t read_bits(const uint8_t* report, ULONG length, uint32_t bit, uint32_t count) {
  uint32_t v = 0;
  for (uint32_t k = 0; k < count; ++k) {
    const uint32_t at = 8 + bit + k;   // skip the report ID byte
    if (at / 8 >= length) break;
    if (report[at / 8] & (1u << (at % 8))) v |= 1u << k;
  }
  return v;
}

bool report_matches(const Parsed& p, const Field& f, const uint8_t* report) {
  return !p.numbered || f.report_id == report[0];
}

uint16_t usage_at(const Field& f, int index) {
  if (f.is_range) return (uint16_t)(f.usage_min + index);
  if (!f.usage_count) return 0;
  return f.usages[std::min<int>(index, f.usage_count - 1)];
}

// ---- hidraw devices ----
struct Device {
  std::string path;
  int fd = -1;
  uint16_t vendor = 0, product = 0, version = 0;
  std::vector<uint8_t> descriptor;
  Parsed parsed{};
  std::string name;
  bool gone = false;
};
std::mutex g_devices_mutex;
std::vector<std::unique_ptr<Device>> g_devices;   // never freed: a handle stays valid for the process
std::atomic<bool> g_running{false};
std::thread g_thread;
linux_hid::ReportFn g_report = nullptr;
linux_hid::RemovedFn g_removed = nullptr;

bool read_descriptor(int fd, std::vector<uint8_t>& out) {
  int size = 0;
  if (ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0) return false;
  hidraw_report_descriptor desc{};
  desc.size = (uint32_t)size;
  if (ioctl(fd, HIDIOCGRDESC, &desc) < 0) return false;
  out.assign(desc.value, desc.value + desc.size);
  return true;
}

Device* find_device_locked(const std::string& path) {
  for (auto& d : g_devices) if (d->path == path) return d.get();
  return nullptr;
}

// The Raw Input registration on Windows takes usage pages 1/4 and 1/5 (joystick, game pad).
bool wanted(const Device& d) {
  if (d.vendor == 0x057E && d.product == 0x0337) return false;   // the GameCube adapter: libusb owns it
  return d.parsed.usage_page == 0x01 && (d.parsed.usage == 0x04 || d.parsed.usage == 0x05);
}

void scan() {
  std::vector<std::string> present;
  if (DIR* dir = opendir("/dev")) {
    while (dirent* e = readdir(dir)) if (!std::strncmp(e->d_name, "hidraw", 6)) present.push_back(std::string("/dev/") + e->d_name);
    closedir(dir);
  }
  std::lock_guard<std::mutex> lock(g_devices_mutex);
  for (auto& d : g_devices) {
    if (d->gone) continue;
    if (std::find(present.begin(), present.end(), d->path) == present.end() || d->fd < 0) continue;
  }
  for (const auto& path : present) {
    Device* existing = find_device_locked(path);
    if (existing && !existing->gone) continue;
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;   // no permission (the udev rule is not installed) or already gone
    auto d = std::make_unique<Device>();
    d->path = path; d->fd = fd;
    hidraw_devinfo info{};
    if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0) { d->vendor = (uint16_t)info.vendor; d->product = (uint16_t)info.product; }
    char name[256] = {};
    if (ioctl(fd, HIDIOCGRAWNAME(sizeof name), name) >= 0) d->name = name;
    if (!read_descriptor(fd, d->descriptor) || !parse_descriptor(d->descriptor.data(), d->descriptor.size(), d->parsed) || !wanted(*d)) {
      close(fd);
      continue;
    }
    if (existing) {   // the same node came back: reuse the handle the readers know
      existing->fd = fd; existing->vendor = d->vendor; existing->product = d->product; existing->name = d->name;
      existing->descriptor = std::move(d->descriptor); existing->parsed = d->parsed; existing->gone = false;
    } else {
      g_devices.push_back(std::move(d));
    }
  }
}

void reader() {
  auto next_scan = std::chrono::steady_clock::now();
  std::vector<uint8_t> buffer(1 + 4096);
  while (g_running.load()) {
    if (std::chrono::steady_clock::now() >= next_scan) { scan(); next_scan = std::chrono::steady_clock::now() + std::chrono::milliseconds(500); }
    std::vector<pollfd> fds; std::vector<Device*> owners;
    {
      std::lock_guard<std::mutex> lock(g_devices_mutex);
      for (auto& d : g_devices) if (!d->gone && d->fd >= 0) { fds.push_back({d->fd, POLLIN, 0}); owners.push_back(d.get()); }
    }
    if (fds.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
    if (poll(fds.data(), fds.size(), 100) <= 0) continue;
    for (size_t i = 0; i < fds.size(); ++i) {
      Device* d = owners[i];
      if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        { std::lock_guard<std::mutex> lock(g_devices_mutex); close(d->fd); d->fd = -1; d->gone = true; }
        if (g_removed) g_removed(d);
        continue;
      }
      if (!(fds[i].revents & POLLIN)) continue;
      for (;;) {
        // Windows puts the report ID in byte 0 and a 0 there for a device without numbered reports.
        const size_t at = d->parsed.numbered ? 0 : 1;
        const ssize_t n = read(d->fd, buffer.data() + at, buffer.size() - at);
        if (n <= 0) break;
        if (!d->parsed.numbered) buffer[0] = 0;
        if (g_report) g_report(d, buffer.data(), (uint32_t)(n + at));
      }
    }
  }
}

Device* device_of(HANDLE h) {
  std::lock_guard<std::mutex> lock(g_devices_mutex);
  for (auto& d : g_devices) if (d.get() == h) return d.get();
  return nullptr;
}

}  // namespace

// A file the readers opened with CreateFileW on a /dev/hidraw path (compat.cpp's handle).
namespace linux_compat { int fd_of_handle(HANDLE file); }

NTSTATUS_HID HidP_GetCaps(PHIDP_PREPARSED_DATA data, HIDP_CAPS* caps) {
  const Parsed* p = parsed_of(data);
  if (!p || !caps) return HIDP_STATUS_INVALID_PREPARSED_DATA;
  std::memset(caps, 0, sizeof *caps);
  caps->Usage = p->usage; caps->UsagePage = p->usage_page;
  caps->InputReportByteLength = p->input_bytes; caps->OutputReportByteLength = p->output_bytes;
  caps->FeatureReportByteLength = p->feature_bytes; caps->NumberLinkCollectionNodes = p->collections;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    const bool button = is_button(f);
    if (f.type == HidP_Input) (button ? caps->NumberInputButtonCaps : caps->NumberInputValueCaps)++;
    else if (f.type == HidP_Output) (button ? caps->NumberOutputButtonCaps : caps->NumberOutputValueCaps)++;
    else (button ? caps->NumberFeatureButtonCaps : caps->NumberFeatureValueCaps)++;
  }
  return HIDP_STATUS_SUCCESS;
}

NTSTATUS_HID HidP_GetButtonCaps(HIDP_REPORT_TYPE type, HIDP_BUTTON_CAPS* caps, USHORT* length, PHIDP_PREPARSED_DATA data) {
  const Parsed* p = parsed_of(data);
  if (!p || !length) return HIDP_STATUS_INVALID_PREPARSED_DATA;
  USHORT n = 0;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    if (f.type != type || !is_button(f)) continue;
    if (n < *length && caps) {
      HIDP_BUTTON_CAPS& c = caps[n];
      std::memset(&c, 0, sizeof c);
      c.UsagePage = f.usage_page; c.ReportID = f.report_id; c.LinkCollection = f.collection; c.IsAbsolute = f.is_absolute;
      c.ReportCount = f.report_count;
      if (f.is_range || f.usage_count > 1) { c.IsRange = 1; c.Range.UsageMin = f.usage_min; c.Range.UsageMax = f.usage_max; }
      else c.NotRange.Usage = f.usage_min;
    }
    ++n;
  }
  const bool fits = n <= *length;
  *length = std::min<USHORT>(n, *length);
  return fits ? HIDP_STATUS_SUCCESS : HIDP_STATUS_BUFFER_TOO_SMALL;
}

NTSTATUS_HID HidP_GetValueCaps(HIDP_REPORT_TYPE type, HIDP_VALUE_CAPS* caps, USHORT* length, PHIDP_PREPARSED_DATA data) {
  const Parsed* p = parsed_of(data);
  if (!p || !length) return HIDP_STATUS_INVALID_PREPARSED_DATA;
  USHORT n = 0;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    if (f.type != type || is_button(f)) continue;
    // Windows lists a value field of several explicit usages as one capability per usage.
    const int entries = f.is_range ? 1 : std::max<int>(1, f.usage_count);
    for (int u = 0; u < entries; ++u) {
      if (n < *length && caps) {
        HIDP_VALUE_CAPS& c = caps[n];
        std::memset(&c, 0, sizeof c);
        c.UsagePage = f.usage_page; c.ReportID = f.report_id; c.LinkCollection = f.collection; c.IsAbsolute = f.is_absolute;
        c.HasNull = f.has_null; c.BitSize = f.report_size; c.ReportCount = f.is_range ? f.report_count : 1;
        c.LogicalMin = f.logical_min; c.LogicalMax = f.logical_max; c.PhysicalMin = f.physical_min; c.PhysicalMax = f.physical_max;
        if (f.is_range) { c.IsRange = 1; c.Range.UsageMin = f.usage_min; c.Range.UsageMax = f.usage_max; }
        else c.NotRange.Usage = usage_at(f, u);
      }
      ++n;
    }
  }
  const bool fits = n <= *length;
  *length = std::min<USHORT>(n, *length);
  return fits ? HIDP_STATUS_SUCCESS : HIDP_STATUS_BUFFER_TOO_SMALL;
}

ULONG HidP_MaxUsageListLength(HIDP_REPORT_TYPE type, USAGE page, PHIDP_PREPARSED_DATA data) {
  const Parsed* p = parsed_of(data);
  if (!p) return 0;
  ULONG n = 0;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    if (f.type != type || !is_button(f) || (page && f.usage_page != page)) continue;
    n += f.is_array ? f.report_count : (f.is_range ? (ULONG)(f.usage_max - f.usage_min + 1) : f.report_count);
  }
  return n;
}

namespace {
template <class Emit> NTSTATUS_HID pressed_usages(HIDP_REPORT_TYPE type, USAGE page, PHIDP_PREPARSED_DATA data,
                                                  PCHAR report, ULONG length, Emit emit) {
  const Parsed* p = parsed_of(data);
  if (!p) return HIDP_STATUS_INVALID_PREPARSED_DATA;
  const auto* r = reinterpret_cast<const uint8_t*>(report);
  if (!r || !length) return HIDP_STATUS_INVALID_REPORT_LENGTH;
  bool any_field = false;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    if (f.type != type || !is_button(f) || (page && f.usage_page != page) || !report_matches(*p, f, r)) continue;
    any_field = true;
    if (f.is_array) {
      // Each slot holds the index of a pressed usage, counted from the logical minimum.
      for (int k = 0; k < f.report_count; ++k) {
        const int32_t index = (int32_t)read_bits(r, length, f.bit_offset + k * f.report_size, f.report_size);
        if (index < f.logical_min || index > f.logical_max) continue;
        const int at = index - f.logical_min;
        const uint16_t usage = f.is_range ? (uint16_t)(f.usage_min + at) : (at < f.usage_count ? f.usages[at] : 0);
        if (usage && !emit(usage, f.usage_page)) return HIDP_STATUS_BUFFER_TOO_SMALL;
      }
    } else {
      for (int k = 0; k < f.report_count; ++k) {
        if (!read_bits(r, length, f.bit_offset + k * f.report_size, 1)) continue;
        const uint16_t usage = usage_at(f, k);
        if (usage && !emit(usage, f.usage_page)) return HIDP_STATUS_BUFFER_TOO_SMALL;
      }
    }
  }
  return any_field || !p->numbered ? HIDP_STATUS_SUCCESS : HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
}
}  // namespace

NTSTATUS_HID HidP_GetUsagesEx(HIDP_REPORT_TYPE type, USHORT, USAGE_AND_PAGE* list, ULONG* length,
                              PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length) {
  if (!length) return HIDP_STATUS_BUFFER_TOO_SMALL;
  ULONG n = 0; const ULONG cap = *length;
  const NTSTATUS_HID s = pressed_usages(type, 0, data, report, report_length, [&](uint16_t usage, uint16_t page) {
    if (n >= cap) return false;
    list[n].Usage = usage; list[n].UsagePage = page; ++n;
    return true;
  });
  *length = n;
  return s;
}

NTSTATUS_HID HidP_GetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT, PUSAGE list, ULONG* length,
                            PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length) {
  if (!length) return HIDP_STATUS_BUFFER_TOO_SMALL;
  ULONG n = 0; const ULONG cap = *length;
  const NTSTATUS_HID s = pressed_usages(type, page, data, report, report_length, [&](uint16_t usage, uint16_t) {
    if (n >= cap) return false;
    list[n++] = usage;
    return true;
  });
  *length = n;
  return s;
}

NTSTATUS_HID HidP_GetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT, USAGE usage, ULONG* value,
                                PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length) {
  const Parsed* p = parsed_of(data);
  if (!p) return HIDP_STATUS_INVALID_PREPARSED_DATA;
  const auto* r = reinterpret_cast<const uint8_t*>(report);
  if (!r || !value) return HIDP_STATUS_INVALID_REPORT_LENGTH;
  for (int i = 0; i < p->field_count; ++i) {
    const Field& f = p->fields[i];
    if (f.type != type || is_button(f) || f.usage_page != page) continue;
    int index = -1;
    if (f.is_range) { if (usage >= f.usage_min && usage <= f.usage_max) index = usage - f.usage_min; }
    else for (int k = 0; k < f.usage_count; ++k) if (f.usages[k] == usage) { index = k; break; }
    if (index < 0 || index >= f.report_count) continue;
    if (!report_matches(*p, f, r)) return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
    // The raw field, unsigned and not sign-extended, as Windows returns it.
    *value = read_bits(r, report_length, f.bit_offset + index * f.report_size, f.report_size);
    return HIDP_STATUS_SUCCESS;
  }
  return HIDP_STATUS_USAGE_NOT_FOUND;
}

BOOL HidD_GetPreparsedData(HANDLE file, PHIDP_PREPARSED_DATA* data) {
  const int fd = linux_compat::fd_of_handle(file);
  if (fd < 0 || !data) return FALSE;
  std::vector<uint8_t> desc;
  if (!read_descriptor(fd, desc)) return FALSE;
  auto* p = new Parsed;
  if (!parse_descriptor(desc.data(), desc.size(), *p)) { delete p; return FALSE; }
  *data = reinterpret_cast<PHIDP_PREPARSED_DATA>(p);
  return TRUE;
}
BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data) { delete reinterpret_cast<Parsed*>(data); return TRUE; }
BOOL HidD_SetOutputReport(HANDLE file, LPVOID report, ULONG length) {
  const int fd = linux_compat::fd_of_handle(file);
  if (fd < 0) return FALSE;
#ifdef HIDIOCSOUTPUT
  if (ioctl(fd, HIDIOCSOUTPUT(length), report) >= 0) return TRUE;
#endif
  return write(fd, report, length) == (ssize_t)length;
}
BOOL HidD_GetProductString(HANDLE file, LPVOID buffer, ULONG length) {
  const int fd = linux_compat::fd_of_handle(file);
  if (fd < 0 || !buffer || length < sizeof(wchar_t)) return FALSE;
  char name[256] = {};
  if (ioctl(fd, HIDIOCGRAWNAME(sizeof name), name) < 0) return FALSE;
  const int chars = MultiByteToWideChar(CP_UTF8, 0, name, -1, static_cast<wchar_t*>(buffer), (int)(length / sizeof(wchar_t)));
  return chars > 0;
}
BOOL HidD_FlushQueue(HANDLE file) {
  const int fd = linux_compat::fd_of_handle(file);
  if (fd < 0) return FALSE;
  const int flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  uint8_t scratch[512];
  while (read(fd, scratch, sizeof scratch) > 0) {}
  fcntl(fd, F_SETFL, flags);
  return TRUE;
}

UINT GetRawInputDeviceInfoW(HANDLE device, UINT command, LPVOID data, UINT* size) {
  Device* d = device_of(device);
  if (!d || !size) return (UINT)-1;
  if (command == RIDI_DEVICENAME) {
    std::wstring w(d->path.begin(), d->path.end());
    const UINT chars = (UINT)w.size() + 1;
    if (!data) { *size = chars; return 0; }
    if (*size < chars) { *size = chars; return (UINT)-1; }
    std::wmemcpy(static_cast<wchar_t*>(data), w.c_str(), chars);
    return chars;
  }
  if (command == RIDI_DEVICEINFO) {
    if (!data) { *size = sizeof(RID_DEVICE_INFO); return 0; }
    if (*size < sizeof(RID_DEVICE_INFO)) { *size = sizeof(RID_DEVICE_INFO); return (UINT)-1; }
    auto* info = static_cast<RID_DEVICE_INFO*>(data);
    info->dwType = RIM_TYPEHID;
    info->hid.dwVendorId = d->vendor; info->hid.dwProductId = d->product; info->hid.dwVersionNumber = d->version;
    info->hid.usUsagePage = d->parsed.usage_page; info->hid.usUsage = d->parsed.usage;
    return sizeof(RID_DEVICE_INFO);
  }
  if (command == RIDI_PREPARSEDDATA) {
    if (!data) { *size = sizeof(Parsed); return 0; }
    if (*size < sizeof(Parsed)) { *size = sizeof(Parsed); return (UINT)-1; }
    std::memcpy(data, &d->parsed, sizeof(Parsed));
    return sizeof(Parsed);
  }
  return (UINT)-1;
}

UINT GetRawInputDeviceList(RAWINPUTDEVICELIST* list, UINT* count, UINT) {
  if (!count) return (UINT)-1;
  std::lock_guard<std::mutex> lock(g_devices_mutex);
  UINT n = 0;
  for (auto& d : g_devices) if (!d->gone) ++n;
  if (!list) { *count = n; return 0; }
  if (*count < n) { *count = n; return (UINT)-1; }
  UINT i = 0;
  for (auto& d : g_devices) if (!d->gone) { list[i].hDevice = d.get(); list[i].dwType = RIM_TYPEHID; ++i; }
  return n;
}

void linux_hid::start(ReportFn report, RemovedFn removed) {
  if (g_running.exchange(true)) return;
  g_report = report; g_removed = removed;
  g_thread = std::thread(reader);
}
void linux_hid::stop() {
  if (!g_running.exchange(false)) return;
  if (g_thread.joinable()) g_thread.join();
}
