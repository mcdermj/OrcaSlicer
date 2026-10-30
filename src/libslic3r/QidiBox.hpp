#pragma once

#include <string>
#include <vector>

namespace Slic3r { namespace QidiBox {

std::string format_slot_line(int filament, int slot);

// Identity map (filament n to slot n) of the filaments that have a slot, between the
// QIDI_BOX_MAP markers.
std::string build_slot_map_block(const std::vector<unsigned int>& filaments, int slot_count);

}} // namespace Slic3r::QidiBox
