// CMake's check that dxc.exe knows HLSL 2021 (-HV 2021), which the ray-tracing shaders use.
template <typename T> T twice(T v) { return v + v; }
RWStructuredBuffer<uint> Out : register(u0);
[numthreads(1, 1, 1)] void main() { Out[0] = twice(1u); }
