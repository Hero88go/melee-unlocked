// Test-bench helper for the latency measurements (WP5): reads and sets an output endpoint's
// "allow exclusive control" switches and its shared-mode format through the interface the Sound
// control panel uses (IPolicyConfig, undocumented, stable since Windows 7). No admin rights needed.
//
//   endpoint_policy <endpoint id> show
//   endpoint_policy <endpoint id> allow-exclusive
//   endpoint_policy <endpoint id> rate 48000
//
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>

#pragma comment(lib, "ole32.lib")

struct DeviceShareMode;
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, INT, const PROPERTYKEY&, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, INT, const PROPERTYKEY&, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
// PKEY_AudioEndpoint_* "allow exclusive" (pid 3) and "exclusive mode priority" (pid 4).
static const PROPERTYKEY kAllow = {{0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}}, 3};
static const PROPERTYKEY kPriority = {{0xb3f8fa53, 0x0004, 0x438e, {0x90, 0x03, 0x51, 0xa4, 0x6e, 0x13, 0x9b, 0xfc}}, 4};

static void show_format(const char* what, const WAVEFORMATEX* f) {
  if (!f) { std::printf("%s: none\n", what); return; }
  std::printf("%s: %lu Hz, %u channels, %u bit, tag %04X\n", what, (unsigned long)f->nSamplesPerSec, (unsigned)f->nChannels,
              (unsigned)f->wBitsPerSample, (unsigned)f->wFormatTag);
}

static long read_flag(IPolicyConfig* pc, PCWSTR id, const PROPERTYKEY& key) {
  PROPVARIANT v; PropVariantInit(&v);
  long out = -1;
  if (SUCCEEDED(pc->GetPropertyValue(id, 0, key, &v))) out = v.vt == VT_UI4 ? (long)v.ulVal : v.vt == VT_BOOL ? (v.boolVal ? 1 : 0) : -(long)v.vt - 100;
  PropVariantClear(&v);
  return out;
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { std::fprintf(stderr, "endpoint_policy <endpoint id> show|allow-exclusive|rate N\n"); return 2; }
  const std::wstring id = argv[1], action = argv[2];
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  IPolicyConfig* pc = nullptr;
  HRESULT hr = CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), (void**)&pc);
  if (FAILED(hr)) { std::fprintf(stderr, "no policy config interface (0x%08lX)\n", (unsigned long)hr); return 2; }
  std::printf("allow exclusive %ld, exclusive priority %ld\n", read_flag(pc, id.c_str(), kAllow), read_flag(pc, id.c_str(), kPriority));
  WAVEFORMATEX* device = nullptr; WAVEFORMATEX* mix = nullptr;
  pc->GetDeviceFormat(id.c_str(), 0, &device); pc->GetMixFormat(id.c_str(), &mix);
  show_format("device format", device); show_format("mix format", mix);
  int rc = 0;
  if (action == L"allow-exclusive") {
    for (const PROPERTYKEY* key : {&kAllow, &kPriority}) {
      PROPVARIANT v; PropVariantInit(&v); v.vt = VT_UI4; v.ulVal = 1;
      hr = pc->SetPropertyValue(id.c_str(), 0, *key, &v);
      std::printf("set pid %lu: 0x%08lX\n", (unsigned long)key->pid, (unsigned long)hr);
      if (FAILED(hr)) rc = 1;
    }
    std::printf("now: allow exclusive %ld, exclusive priority %ld\n", read_flag(pc, id.c_str(), kAllow), read_flag(pc, id.c_str(), kPriority));
  } else if (action == L"rate" && argc > 3 && device && mix) {
    const DWORD rate = (DWORD)_wtoi(argv[3]);
    for (WAVEFORMATEX* f : {device, mix}) { f->nSamplesPerSec = rate; f->nAvgBytesPerSec = rate * f->nBlockAlign; }
    hr = pc->SetDeviceFormat(id.c_str(), device, mix);
    std::printf("set format: 0x%08lX\n", (unsigned long)hr);
    if (FAILED(hr)) rc = 1;
    WAVEFORMATEX* d2 = nullptr; WAVEFORMATEX* m2 = nullptr;
    pc->GetDeviceFormat(id.c_str(), 0, &d2); pc->GetMixFormat(id.c_str(), &m2);
    show_format("device format now", d2); show_format("mix format now", m2);
    CoTaskMemFree(d2); CoTaskMemFree(m2);
  }
  CoTaskMemFree(device); CoTaskMemFree(mix);
  pc->Release();
  CoUninitialize();
  return rc;
}
