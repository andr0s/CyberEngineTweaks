#pragma once

// RED4ext.SDK compatibility. The Windows build pins an SDK whose DynArray has public fields (size, capacity, entries);
// RED4ext.SDK-macos is based on a newer SDK where they are methods (Size(), Capacity(), Data()). These helpers work
// with both.

template <class TArray> [[nodiscard]] constexpr auto RedSize(const TArray& acArray)
{
    if constexpr (requires { acArray.Size(); })
        return acArray.Size();
    else
        return acArray.size;
}

template <class TArray> [[nodiscard]] constexpr auto RedData(TArray& aArray)
{
    if constexpr (requires { aArray.Data(); })
        return aArray.Data();
    else
        return aArray.entries;
}
