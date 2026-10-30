#include "QidiBox.hpp"

#include <string>

namespace Slic3r { namespace QidiBox {

std::string format_slot_line(int filament, int slot)
{
    // SAVE_VARIABLE reads VALUE as a Python literal, so the slot name keeps its own quotes
    return "SAVE_VARIABLE VARIABLE=value_t" + std::to_string(filament) + " VALUE=\"'slot" + std::to_string(slot) + "'\"";
}

}} // namespace Slic3r::QidiBox
