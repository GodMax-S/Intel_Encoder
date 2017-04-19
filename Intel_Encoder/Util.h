#pragma once

namespace Util
{
    
    inline int RoundUp(int value, int round_to)
    {
        return ((value + round_to - 1) / round_to) * round_to;
    }

}