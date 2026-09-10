#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#pragma comment(lib, "d3d11.lib")
int main()
{
  using Microsoft::WRL::ComPtr;
  for (UINT flags : {0u, 1u})
  {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    auto hr = D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,
        nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);
    std::cout << "device flags=" << flags << " hr=" << std::hex << hr << '\n';
    if (FAILED(hr)) return 1;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width=640;desc.Height=528;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> texture;
    hr=device->CreateTexture2D(&desc,nullptr,&texture);
    std::cout << "shared texture hr=" << std::hex << hr << '\n';
  }
}
