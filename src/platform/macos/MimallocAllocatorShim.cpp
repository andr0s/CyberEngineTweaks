// TiltedCore's default allocator is MimallocAllocator. The macOS build does not ship mimalloc; this file replaces
// vendor/TiltedCore/Code/core/src/MimallocAllocator.cpp with the same class on top of the system allocator.

#include <MimallocAllocator.hpp>

#include <cstdlib>
#include <malloc/malloc.h>

namespace TiltedPhoques
{
void* MimallocAllocator::Allocate(const size_t aSize) noexcept
{
    return malloc(aSize);
}

void MimallocAllocator::Free(void* apData) noexcept
{
    free(apData);
}

size_t MimallocAllocator::Size(void* apData) noexcept
{
    return apData ? malloc_size(apData) : 0;
}

void* MimallocAllocator::AlignedAllocate(const size_t aSize, const size_t aAlignment) noexcept
{
    void* pData = nullptr;
    const auto alignment = aAlignment < sizeof(void*) ? sizeof(void*) : aAlignment;
    if (posix_memalign(&pData, alignment, aSize) != 0)
        return nullptr;
    return pData;
}

void MimallocAllocator::AlignedFree(void* apData) noexcept
{
    free(apData);
}
} // namespace TiltedPhoques
