#include "QidiBox.hpp"

#include <string>
#include <vector>

namespace Slic3r { namespace QidiBox {

std::string format_slot_line(int filament, int slot)
{
    // SAVE_VARIABLE reads VALUE as a Python literal, so the slot name keeps its own quotes
    return "SAVE_VARIABLE VARIABLE=value_t" + std::to_string(filament) + " VALUE=\"'slot" + std::to_string(slot) + "'\"";
}

std::string build_slot_map_block(const std::vector<unsigned int>& filaments, int slot_count)
{
    std::string block = "; QIDI_BOX_MAP_BEGIN\n";
    for (const unsigned int n : filaments)
        if (static_cast<int>(n) < slot_count)
            block += format_slot_line(static_cast<int>(n), static_cast<int>(n)) + "\n";
    return block + "; QIDI_BOX_MAP_END\n";
}

}} // namespace Slic3r::QidiBox
