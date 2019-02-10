#pragma once

#include <array>
#include <string>

namespace mnemonic
{
    typedef std::array<std::string, 2048> Dictionary;

    namespace language
    {
        extern const Dictionary en;
    }
}
