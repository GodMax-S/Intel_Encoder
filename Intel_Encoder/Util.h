#pragma once
#include <string>

namespace Util
{
    template<typename T, typename K>
    inline int RoundUp(T value, K round_to)
    {
        return ((value + round_to - 1) / round_to) * round_to;
    }

    inline void HandleMFXErrors(const mfxStatus status, const std::string& where_str = "Unspecified operation")
    {
        if (status < MFX_ERR_NONE)
        {
            std::cerr << "Error " << status << " during " << where_str << "\n";
            getchar();
        }
    }

    template<typename T>
    inline int FindFreeSurfaceIndex(const T& surfaces, int size)
    {
        for (int i = 0; i < size; i++)
        {
            if (!surfaces[i]->Data.Locked)
                return i;
        }
        return -1;
    };
}