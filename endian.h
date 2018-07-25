#pragma once

namespace tcpshm {

template<bool ToLittle>
class Endian
{
public:
    static constexpr bool IsLittle = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

    template<class T>
    static T Convert(T t) {
        if(ToLittle == IsLittle) return t; // compile time check

        if(sizeof(T) == 2)
            return __builtin_bswap16((uint16_t)t);
        else if(sizeof(T) == 4)
            return __builtin_bswap32((uint32_t)t);
        else if(sizeof(T) == 8)
            return __builtin_bswap64((uint64_t)t);
        else
            return t;
    }

    template<class T>
    static void ConvertInPlace(T& t) {
        t = Convert(t);
    }
};
} // namespace tcpshm

