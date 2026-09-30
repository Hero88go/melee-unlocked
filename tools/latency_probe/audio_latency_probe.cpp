// Key press to sound: records what Windows mixes for the default output device (WASAPI loopback,
// with the audio engine's own QPC time of every packet) while it presses keys in a game window at
// known QPC times. The analysis (run-source/latency-20260929/audio_latency.py) finds each press's
// sound in the recording. Loopback taps the mix, so the output device's own delay after the mix is
// not included; it is the same for every program measured on the same device.
//
// The target process's audio session is set to --session-volume (default 0.15) for the run and put
// back afterwards, so the test stays quiet on a normal output.
//
//   audio_latency_probe --pid N --hwnd 0x... --trials 40 --gap-ms 700 --out prefix
//
// Writes prefix.wav (the mix, float32), prefix-packets.csv (first sample of each packet, its time in
// 100 ns QPC units) and prefix-presses.csv (each press's time in the same units, and the key).
//
// With a cable from an interface's output to its input, --capture-device records that input instead
// of the loopback, so the measurement includes the output's DAC and the whole device path:
//
//   audio_latency_probe --list-devices
//   audio_latency_probe --level-check --render-device MOTU --capture-device "In 1-2" --out level
//   audio_latency_probe --render-device MOTU --capture-device "In 1-2" --pid N --hwnd 0x... --out prefix
//
// A device is chosen by its endpoint ID or by part of its name. --list-devices prints every active
// endpoint with its shared-mode engine periods (IAudioClient3) for its mix format. --level-check plays
// one short tone (--tone-db, default -30 dBFS, --tone-ms 500) on the render device and reports the
// input's noise floor, peak per channel and clipped samples.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

namespace {

LONGLONG qpc_freq() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }
// The capture client reports packet times as QPC converted to 100 ns units; presses use the same.
unsigned long long now_100ns() {
  static const LONGLONG freq = qpc_freq();
  LARGE_INTEGER c; QueryPerformanceCounter(&c);
  return (unsigned long long)((double)c.QuadPart * 1e7 / (double)freq);
}

struct Packet { unsigned long long first_sample, qpc100; };

std::atomic<bool> g_run{true};
std::vector<float> g_samples;      // interleaved, g_channels per frame
std::vector<Packet> g_packets;
WORD g_channels = 2;
DWORD g_rate = 48000;

bool capture_thread_body(IAudioClient* client, IAudioCaptureClient* capture) {
  unsigned long long frames = 0;
  while (g_run.load()) {
    Sleep(1);
    UINT32 packet = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
      BYTE* data = nullptr; UINT32 n = 0; DWORD flags = 0; UINT64 pos = 0, qpc = 0;
      if (FAILED(capture->GetBuffer(&data, &n, &flags, &pos, &qpc))) return false;
      g_packets.push_back({frames, qpc});
      const size_t base = g_samples.size();
      g_samples.resize(base + (size_t)n * g_channels);
      if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::memset(&g_samples[base], 0, (size_t)n * g_channels * sizeof(float));
      else std::memcpy(&g_samples[base], data, (size_t)n * g_channels * sizeof(float));
      frames += n;
      capture->ReleaseBuffer(n);
    }
  }
  return true;
}

// The session volume of every audio session owned by `pid` on the default device.
std::vector<std::pair<ISimpleAudioVolume*, float>> set_session_volume(IMMDevice* device, DWORD pid, float volume) {
  std::vector<std::pair<ISimpleAudioVolume*, float>> changed;
  if (pid == 0) return changed;   // PID 0 is Windows' own system-sounds session: never touch it
  IAudioSessionManager2* manager = nullptr;
  if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&manager))) return changed;
  IAudioSessionEnumerator* list = nullptr;
  if (SUCCEEDED(manager->GetSessionEnumerator(&list))) {
    int count = 0; list->GetCount(&count);
    for (int i = 0; i < count; ++i) {
      IAudioSessionControl* control = nullptr;
      if (FAILED(list->GetSession(i, &control))) continue;
      IAudioSessionControl2* control2 = nullptr;
      if (SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&control2))) {
        DWORD owner = 0;
        if (SUCCEEDED(control2->GetProcessId(&owner)) && owner == pid) {
          ISimpleAudioVolume* v = nullptr;
          if (SUCCEEDED(control->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&v))) {
            float before = 1.0f; v->GetMasterVolume(&before);
            v->SetMasterVolume(volume, nullptr);
            changed.push_back({v, before});
          }
        }
        control2->Release();
      }
      control->Release();
    }
    list->Release();
  }
  manager->Release();
  return changed;
}

void send_key(WORD vk, bool up) {
  INPUT in{}; in.type = INPUT_KEYBOARD;
  in.ki.wScan = (WORD)MapVirtualKeyW(vk, 0);
  in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | (up ? KEYEVENTF_KEYUP : 0);
  SendInput(1, &in, sizeof in);
}

// PKEY_Device_FriendlyName, spelled out so no GUID header has to be instantiated here.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string narrow(const wchar_t* w) {
  if (!w) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(n > 0 ? n - 1 : 0, '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

std::string device_id(IMMDevice* d) {
  LPWSTR id = nullptr;
  std::string s;
  if (SUCCEEDED(d->GetId(&id))) { s = narrow(id); CoTaskMemFree(id); }
  return s;
}

std::string device_name(IMMDevice* d) {
  IPropertyStore* props = nullptr;
  std::string s;
  if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &props))) {
    PROPVARIANT v; PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) s = narrow(v.pwszVal);
    PropVariantClear(&v);
    props->Release();
  }
  return s;
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// The first active endpoint of `flow` whose ID equals `spec` or whose name contains it.
IMMDevice* find_device(IMMDeviceEnumerator* e, EDataFlow flow, const std::string& spec) {
  IMMDeviceCollection* all = nullptr;
  if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &all))) return nullptr;
  UINT n = 0; all->GetCount(&n);
  IMMDevice* found = nullptr;
  for (UINT i = 0; i < n && !found; ++i) {
    IMMDevice* d = nullptr;
    if (FAILED(all->Item(i, &d))) continue;
    if (device_id(d) == spec || lower(device_name(d)).find(lower(spec)) != std::string::npos) found = d;
    else d->Release();
  }
  all->Release();
  return found;
}

int list_devices(IMMDeviceEnumerator* e) {
  for (EDataFlow flow : {eRender, eCapture}) {
    IMMDeviceCollection* all = nullptr;
    if (FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &all))) continue;
    UINT n = 0; all->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
      IMMDevice* d = nullptr;
      if (FAILED(all->Item(i, &d))) continue;
      std::printf("%s  %s\n    id %s\n", flow == eRender ? "output" : "input ", device_name(d).c_str(), device_id(d).c_str());
      IAudioClient3* c3 = nullptr;
      WAVEFORMATEX* mix = nullptr;
      if (SUCCEEDED(d->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr, (void**)&c3)) && SUCCEEDED(c3->GetMixFormat(&mix))) {
        UINT32 def = 0, fund = 0, mn = 0, mx = 0;
        const double ms = 1000.0 / (double)mix->nSamplesPerSec;
        if (SUCCEEDED(c3->GetSharedModeEnginePeriod(mix, &def, &fund, &mn, &mx)))
          std::printf("    mix %lu Hz, %u channels, %u bit; engine period default %.2f ms, min %.2f ms, max %.2f ms, step %.2f ms\n",
                      (unsigned long)mix->nSamplesPerSec, (unsigned)mix->nChannels, (unsigned)mix->wBitsPerSample,
                      def * ms, mn * ms, mx * ms, fund * ms);
        else
          std::printf("    mix %lu Hz, %u channels, %u bit; no IAudioClient3 period query\n",
                      (unsigned long)mix->nSamplesPerSec, (unsigned)mix->nChannels, (unsigned)mix->wBitsPerSample);
        CoTaskMemFree(mix);
      }
      if (c3) c3->Release();
      d->Release();
    }
    all->Release();
  }
  return 0;
}

// Plays silence, then a 1 kHz tone at `db` dBFS for `ms`, then silence, on `render`, while the capture
// thread records.
bool play_tone(IMMDevice* render, float db, int ms) {
  IAudioClient* out = nullptr;
  WAVEFORMATEX* fmt = nullptr;
  if (FAILED(render->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&out)) || FAILED(out->GetMixFormat(&fmt))) return false;
  if (FAILED(out->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 50 * 10000, 0, fmt, nullptr))) { out->Release(); return false; }
  IAudioRenderClient* rc = nullptr;
  out->GetService(__uuidof(IAudioRenderClient), (void**)&rc);
  UINT32 size = 0; out->GetBufferSize(&size);
  const unsigned rate = fmt->nSamplesPerSec, ch = fmt->nChannels;
  const double amp = std::pow(10.0, db / 20.0);
  const unsigned long long lead = rate * 3 / 10, tone = (unsigned long long)rate * ms / 1000, tail = rate * 6 / 10;
  unsigned long long written = 0;
  out->Start();
  while (written < lead + tone + tail) {
    UINT32 pad = 0; out->GetCurrentPadding(&pad);
    UINT32 room = size - pad;
    if (room) {
      BYTE* data = nullptr;
      if (FAILED(rc->GetBuffer(room, &data))) break;
      float* f = reinterpret_cast<float*>(data);
      for (UINT32 k = 0; k < room; ++k) {
        const unsigned long long s = written + k;
        float v = 0.0f;
        if (s >= lead && s < lead + tone) {
          // 5 ms fades so the tone does not click
          const double t = (double)(s - lead), edge = rate * 0.005;
          const double env = std::min(1.0, std::min(t / edge, (double)(lead + tone - s) / edge));
          v = (float)(amp * env * std::sin(2.0 * 3.14159265358979 * 1000.0 * t / rate));
        }
        for (unsigned c = 0; c < ch; ++c) f[k * ch + c] = v;
      }
      rc->ReleaseBuffer(room, 0);
      written += room;
    }
    Sleep(5);
  }
  Sleep(200);
  out->Stop();
  rc->Release();
  CoTaskMemFree(fmt);
  out->Release();
  return true;
}

void report_level() {
  const size_t frames = g_samples.size() / g_channels;
  const size_t quiet = std::min(frames, (size_t)g_rate / 4);   // the first 250 ms: before the tone arrives
  std::printf("input: %u channels, %.2f s recorded\n", (unsigned)g_channels, (double)frames / g_rate);
  for (unsigned c = 0; c < g_channels; ++c) {
    double floor2 = 0.0, peak = 0.0; size_t clipped = 0;
    for (size_t k = 0; k < frames; ++k) {
      const double v = std::fabs((double)g_samples[k * g_channels + c]);
      if (k < quiet) floor2 += v * v;
      peak = std::max(peak, v);
      if (v >= 0.999) ++clipped;
    }
    const double floor_db = quiet ? 10.0 * std::log10(std::max(floor2 / quiet, 1e-20)) : -200.0;
    std::printf("  channel %u: noise floor %.1f dBFS RMS, peak %.1f dBFS, %zu clipped samples\n", c + 1, floor_db,
                20.0 * std::log10(std::max(peak, 1e-10)), clipped);
  }
}

void write_wav(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  const unsigned bytes = (unsigned)(g_samples.size() * sizeof(float));
  auto u32 = [&](unsigned v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](unsigned short v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
  std::fwrite("fmt ", 1, 4, f); u32(16); u16(3); u16(g_channels); u32(g_rate);
  u32(g_rate * g_channels * 4); u16((unsigned short)(g_channels * 4)); u16(32);
  std::fwrite("data", 1, 4, f); u32(bytes);
  std::fwrite(g_samples.data(), 1, bytes, f);
  std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
  DWORD pid = 0; HWND hwnd = nullptr; int trials = 40, gap_ms = 700, tone_ms = 500; float session_volume = 0.15f, tone_db = -30.0f;
  std::string out = "audio-latency", render_spec, capture_spec;
  bool list = false, level = false;
  int record_seconds = 0;   // only record the input for this long (no key presses, no tone)
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--pid") pid = (DWORD)std::strtoul(next(), nullptr, 0);
    else if (a == "--hwnd") hwnd = (HWND)(uintptr_t)std::strtoull(next(), nullptr, 0);
    else if (a == "--trials") trials = std::atoi(next());
    else if (a == "--gap-ms") gap_ms = std::atoi(next());
    else if (a == "--session-volume") session_volume = (float)std::atof(next());
    else if (a == "--out") out = next();
    else if (a == "--render-device") render_spec = next();
    else if (a == "--capture-device") capture_spec = next();
    else if (a == "--list-devices") list = true;
    else if (a == "--level-check") level = true;
    else if (a == "--record-seconds") record_seconds = std::atoi(next());
    else if (a == "--tone-db") tone_db = std::min(-6.0f, (float)std::atof(next()));
    else if (a == "--tone-ms") tone_ms = std::max(50, std::min(2000, std::atoi(next())));
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
    std::fprintf(stderr, "no audio device enumerator\n"); return 2;
  }
  if (list) return list_devices(enumerator);
  // `device` is the output the game plays to (its session volume is set there); `source` is what is
  // recorded: that output's loopback, or a chosen input.
  IMMDevice* device = nullptr;
  if (render_spec.empty() ? FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))
                          : !(device = find_device(enumerator, eRender, render_spec))) {
    std::fprintf(stderr, "no output device %s\n", render_spec.c_str()); return 2;
  }
  IMMDevice* source = device;
  if (!capture_spec.empty() && !(source = find_device(enumerator, eCapture, capture_spec))) {
    std::fprintf(stderr, "no input device %s\n", capture_spec.c_str()); return 2;
  }
  IAudioClient* client = nullptr;
  WAVEFORMATEX* mix = nullptr;
  if (FAILED(source->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) || FAILED(client->GetMixFormat(&mix))) {
    std::fprintf(stderr, "cannot open the device\n"); return 2;
  }
  // The shared-mode mix format is float32; keep it as it is.
  g_channels = mix->nChannels; g_rate = mix->nSamplesPerSec;
  const DWORD flags = source == device ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
  if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 20 * 10000, 0, mix, nullptr))) {
    std::fprintf(stderr, "%s initialize failed\n", flags ? "loopback" : "input"); return 2;
  }
  std::printf("output %s\ninput  %s%s\n", device_name(device).c_str(), device_name(source).c_str(), flags ? " (loopback)" : "");
  IAudioCaptureClient* capture = nullptr;
  client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture);
  g_samples.reserve((size_t)g_rate * g_channels * (trials * (gap_ms + 200) / 1000 + 10));
  client->Start();
  std::thread worker([&] { capture_thread_body(client, capture); });
  if (record_seconds > 0) {
    Sleep((DWORD)record_seconds * 1000);
    g_run.store(false);
    worker.join();
    client->Stop();
    report_level();
    write_wav(out + ".wav");
    return 0;
  }
  if (level) {
    Sleep(300);   // the noise floor, before the tone
    const bool played = play_tone(device, tone_db, tone_ms);
    g_run.store(false);
    worker.join();
    client->Stop();
    if (!played) { std::fprintf(stderr, "could not play the tone on the output\n"); return 2; }
    std::printf("tone %.0f dBFS for %d ms on the output\n", tone_db, tone_ms);
    report_level();
    write_wav(out + ".wav");
    return 0;
  }
  auto restore = set_session_volume(device, pid, session_volume);
  std::printf("device %u Hz, %u channels; %zu audio session(s) of pid %lu set to %.0f%%\n", (unsigned)g_rate,
              (unsigned)g_channels, restore.size(), (unsigned long)pid, session_volume * 100.0f);
  std::fflush(stdout);
  FILE* presses = std::fopen((out + "-presses.csv").c_str(), "w");
  if (presses) std::fprintf(presses, "qpc100,key\n");
  Sleep(1500);   // a quiet stretch before the first press, to learn the level of the background
  for (int t = 0; t < trials; ++t) {
    if (hwnd && GetForegroundWindow() != hwnd) SetForegroundWindow(hwnd);
    const WORD vk = (t & 1) ? VK_UP : VK_DOWN;
    const unsigned long long when = now_100ns();
    send_key(vk, false);
    Sleep(70);
    send_key(vk, true);
    if (presses) { std::fprintf(presses, "%llu,%s\n", when, vk == VK_UP ? "up" : "down"); std::fflush(presses); }
    Sleep(gap_ms + (t * 37) % 211);   // not a fixed period, so presses do not lock to the game's frame phase
  }
  Sleep(600);
  for (auto& v : restore) { v.first->SetMasterVolume(v.second, nullptr); v.first->Release(); }
  g_run.store(false);
  worker.join();
  client->Stop();
  if (presses) std::fclose(presses);
  FILE* pk = std::fopen((out + "-packets.csv").c_str(), "w");
  if (pk) {
    std::fprintf(pk, "first_sample,qpc100\n");
    for (const auto& p : g_packets) std::fprintf(pk, "%llu,%llu\n", p.first_sample, p.qpc100);
    std::fclose(pk);
  }
  write_wav(out + ".wav");
  std::printf("recorded %.1f s, %zu packets, %d presses\n", (double)g_samples.size() / g_channels / g_rate, g_packets.size(), trials);
  return 0;
}
