// Characters in duels they were not made for: choosing a variant whose model is on the disc.

#include "game/characters.h"

#include <cctype>
#include <cstdint>
#include <string>

#include "core/log.h"
#include "game/resources.h"

namespace swrots::game {

int VariantOnDisc(const void* character, int preferred)
{
    if (!character)
        return preferred;
    const uint8_t* list = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(character) + 0x1E0);
    int first = -1;
    for (int i = 0; list && i < 32; ++i) {
        const uint8_t* record = list + 0x2C + i * 20;
        const char* name = *reinterpret_cast<const char* const*>(record);
        const char* mesh = *reinterpret_cast<const char* const*>(record + 4);
        if (!name)
            break;
        if (!mesh)
            continue;
        std::string path = std::string("meshes\\chars\\") + mesh + ".msh";
        for (char& c : path)
            c = char(tolower(static_cast<unsigned char>(c)));
        if (!DiscHasResource(path))
            continue;
        if (i == preferred)
            return i;
        if (first < 0)
            first = i;
    }
    if (first < 0 || first == preferred)
        return preferred;
    const char* name = *reinterpret_cast<const char* const*>(list + 0x2C + first * 20);
    LOG_INFO("Characters: variant %s (%d) instead of %d, whose model is not on the disc", name, first, preferred);
    return first;
}

} // namespace swrots::game
