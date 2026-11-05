AMD FSR SDK 2.4.0 (https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v2.4.0), API headers only.

Unmodified upstream files, in the SDK's own layout under Kits/FidelityFX:
  api/include/                    ffx_api.h, ffx_api_types.h, ffx_api_loader.h, dx12/, vk/
  upscalers/include/ffx_upscale.h
  framegeneration/include/        ffx_framegeneration.h, ffx_framegeneration_api_types.h, dx12/, vk/
  LICENSE.md                      Kits/FidelityFX/docs/license.md

The headers are MIT licensed. The program loads AMD's pre-built, signed DLLs at run time and works
without them (the FSR modes are then not offered). They are not in this repository: the game fetches
them from the tag above when the player asks (host/updater.cpp fsr_download) and checks each file's
SHA-256 before putting it beside the game. AMD's licence allows their binary redistribution with its
notice (LICENSE.md, "REDISTRIBUTION"); AMD requires applications to use these signed DLLs.
