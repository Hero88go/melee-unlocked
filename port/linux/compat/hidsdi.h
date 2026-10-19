// Linux build: the Raw Input device queries and HID parsing calls the controller readers make
// (hid_pad.cpp, switch_pro.cpp, playstation_pad.cpp), over /dev/hidraw and a HID report descriptor
// parser (port/linux/hid_linux.cpp). Reports reach the readers in Windows' layout: byte 0 is the
// report ID, 0 for a device without numbered reports. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "windows.h"

typedef USHORT USAGE;
typedef USAGE* PUSAGE;
typedef char* PCHAR;
typedef long NTSTATUS_HID;
typedef struct _HIDP_PREPARSED_DATA* PHIDP_PREPARSED_DATA;
typedef enum _HIDP_REPORT_TYPE { HidP_Input, HidP_Output, HidP_Feature } HIDP_REPORT_TYPE;
#define HIDP_STATUS_SUCCESS ((NTSTATUS_HID)0x00110000L)
#define HIDP_STATUS_INVALID_PREPARSED_DATA ((NTSTATUS_HID)0xC0110001L)
#define HIDP_STATUS_INVALID_REPORT_TYPE ((NTSTATUS_HID)0xC0110002L)
#define HIDP_STATUS_INVALID_REPORT_LENGTH ((NTSTATUS_HID)0xC0110003L)
#define HIDP_STATUS_USAGE_NOT_FOUND ((NTSTATUS_HID)0xC0110004L)
#define HIDP_STATUS_BUFFER_TOO_SMALL ((NTSTATUS_HID)0xC0110007L)
#define HIDP_STATUS_INCOMPATIBLE_REPORT_ID ((NTSTATUS_HID)0xC011000AL)

typedef struct _USAGE_AND_PAGE { USAGE Usage; USAGE UsagePage; } USAGE_AND_PAGE;
typedef struct _HIDP_CAPS {
  USAGE Usage, UsagePage;
  USHORT InputReportByteLength, OutputReportByteLength, FeatureReportByteLength;
  USHORT Reserved[17];
  USHORT NumberLinkCollectionNodes, NumberInputButtonCaps, NumberInputValueCaps, NumberInputDataIndices,
         NumberOutputButtonCaps, NumberOutputValueCaps, NumberOutputDataIndices, NumberFeatureButtonCaps,
         NumberFeatureValueCaps, NumberFeatureDataIndices;
} HIDP_CAPS;
typedef struct _HIDP_RANGE { USAGE UsageMin, UsageMax; USHORT StringMin, StringMax, DesignatorMin, DesignatorMax, DataIndexMin, DataIndexMax; } HIDP_RANGE_COMPAT;
typedef struct _HIDP_NOTRANGE { USAGE Usage, Reserved1; USHORT StringIndex, Reserved2, DesignatorIndex, Reserved3, DataIndex, Reserved4; } HIDP_NOTRANGE_COMPAT;
typedef struct _HIDP_BUTTON_CAPS {
  USAGE UsagePage; UCHAR ReportID; BOOLEAN IsAlias; USHORT BitField, LinkCollection; USAGE LinkUsage, LinkUsagePage;
  BOOLEAN IsRange, IsStringRange, IsDesignatorRange, IsAbsolute; USHORT ReportCount, Reserved2; ULONG Reserved[9];
  union { HIDP_RANGE_COMPAT Range; HIDP_NOTRANGE_COMPAT NotRange; };
} HIDP_BUTTON_CAPS;
typedef struct _HIDP_VALUE_CAPS {
  USAGE UsagePage; UCHAR ReportID; BOOLEAN IsAlias; USHORT BitField, LinkCollection; USAGE LinkUsage, LinkUsagePage;
  BOOLEAN IsRange, IsStringRange, IsDesignatorRange, IsAbsolute, HasNull; UCHAR Reserved; USHORT BitSize, ReportCount;
  USHORT Reserved2[5]; ULONG UnitsExp, Units; LONG LogicalMin, LogicalMax, PhysicalMin, PhysicalMax;
  union { HIDP_RANGE_COMPAT Range; HIDP_NOTRANGE_COMPAT NotRange; };
} HIDP_VALUE_CAPS;

NTSTATUS_HID HidP_GetCaps(PHIDP_PREPARSED_DATA data, HIDP_CAPS* caps);
NTSTATUS_HID HidP_GetButtonCaps(HIDP_REPORT_TYPE type, HIDP_BUTTON_CAPS* caps, USHORT* length, PHIDP_PREPARSED_DATA data);
NTSTATUS_HID HidP_GetValueCaps(HIDP_REPORT_TYPE type, HIDP_VALUE_CAPS* caps, USHORT* length, PHIDP_PREPARSED_DATA data);
ULONG HidP_MaxUsageListLength(HIDP_REPORT_TYPE type, USAGE page, PHIDP_PREPARSED_DATA data);
NTSTATUS_HID HidP_GetUsagesEx(HIDP_REPORT_TYPE type, USHORT collection, USAGE_AND_PAGE* list, ULONG* length,
                              PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
NTSTATUS_HID HidP_GetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT collection, PUSAGE list, ULONG* length,
                            PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
NTSTATUS_HID HidP_GetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT collection, USAGE usage, ULONG* value,
                                PHIDP_PREPARSED_DATA data, PCHAR report, ULONG report_length);
BOOL HidD_GetPreparsedData(HANDLE file, PHIDP_PREPARSED_DATA* data);
BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data);
BOOL HidD_SetOutputReport(HANDLE file, LPVOID report, ULONG length);
BOOL HidD_GetProductString(HANDLE file, LPVOID buffer, ULONG length);
BOOL HidD_FlushQueue(HANDLE file);

// ---- Raw Input device queries ----
#define RIM_TYPEMOUSE 0u
#define RIM_TYPEKEYBOARD 1u
#define RIM_TYPEHID 2u
#define RIDI_PREPARSEDDATA 0x20000005u
#define RIDI_DEVICENAME 0x20000007u
#define RIDI_DEVICEINFO 0x2000000bu
#define RID_INPUT 0x10000003u
typedef struct _RID_DEVICE_INFO_HID { DWORD dwVendorId, dwProductId, dwVersionNumber; USHORT usUsagePage, usUsage; } RID_DEVICE_INFO_HID;
typedef struct _RID_DEVICE_INFO { DWORD cbSize, dwType; union { RID_DEVICE_INFO_HID hid; }; } RID_DEVICE_INFO;
typedef struct tagRAWINPUTDEVICELIST { HANDLE hDevice; DWORD dwType; } RAWINPUTDEVICELIST;
UINT GetRawInputDeviceInfoW(HANDLE device, UINT command, LPVOID data, UINT* size);
UINT GetRawInputDeviceList(RAWINPUTDEVICELIST* list, UINT* count, UINT size);

namespace linux_hid {
// Starts the reader: every /dev/hidraw node that is a game pad or joystick (the usages Windows'
// Raw Input registration takes), its reports handed to `report` and its removal to `removed`, on the
// reader's own thread. Devices are rescanned twice a second.
using ReportFn = void (*)(HANDLE device, const uint8_t* report, uint32_t size);
using RemovedFn = void (*)(HANDLE device);
void start(ReportFn report, RemovedFn removed);
void stop();
}
