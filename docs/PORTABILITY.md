# Portability

The primary implementation target is C++20 on Windows 11 x64 with MSVC and CMake. The build is kept architecturally portable.

## Windows

Validated with MSVC under `/W4 /WX /EHsc /utf-8`. Shared memory uses real Windows file mapping; the file-backed allocator uses real mapping primitives.

## Vendor neutrality

Allocator Lab provides clean backend/provider seams for host, aligned host, pinned host, accelerator-local device, shared, mapped, and caller-provided memory. It does not depend architecturally on a single accelerator vendor.

## Supported vs. labeled

CUDA is implemented and validated on the available NVIDIA RTX 5090 as a real measured backend. HIP, ROCm, Level Zero, Vulkan, Metal, and CXL are not claimed unless a real implementation exists; those may have clean extension seams but are labeled honest in capabilities.

## Unsupported in this environment

- HIP / ROCm / Level Zero / Vulkan / Metal / CXL backends: not implemented, not claimed.
- Linux / macOS builds: not validated in this environment (the code is written portably but only Windows is tested here).
