#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace QidiBox {

std::string format_slot_line(int filament, int slot);

// Identity map (filament n to slot n) of the filaments that have a slot, between the
// QIDI_BOX_MAP markers.
std::string build_slot_map_block(const std::vector<unsigned int>& filaments, int slot_count);

// Reads one line written by format_slot_line(). Returns false for any other line.
bool parse_slot_line(const std::string& line, int& filament, int& slot);

// What the QIDI_BOX blocks at the head of a G-code file say.
struct Blocks
{
    bool                             has_map  = false;
    bool                             has_temp = false;
    std::vector<std::pair<int, int>> map_slots;      // (filament, slot) of each value_t line
    std::vector<int>                 temp_filaments; // n of each VTn in BOX_TEMP_SET
};

Blocks parse_blocks(const std::string& gcode);

// What one Box slot holds. `type` is the normalized material type.
struct SlotState
{
    bool        has_filament = false;
    std::string type;
};

// What the printer reported at send time. `slots` is indexed by slot.
struct BoxState
{
    int                    box_count          = 0;
    bool                   box_temp_supported = false;
    std::vector<SlotState> slots;
};

// A reason to refuse a send. `filament` is 0-based; `slot` is the slot the G-code or the dialog names, -1 when none.
// `filament_type` and `slot_type` are set for TypeMismatch.
struct Problem
{
    enum class Kind { MapDisagrees, SlotBeyondBoxes, SlotEmpty, TypeMismatch, TempBeyondBoxes, CommandsMissing };

    Kind        kind;
    int         filament = -1;
    int         slot     = -1;
    std::string filament_type;
    std::string slot_type;
};

// Reads the print dialog's mapping, one Box slot per filament, -1 where a filament has none. Returns false when `ams_mapping`
// is not a JSON array.
bool parse_slot_mapping(const std::string& ams_mapping, std::vector<int>& mapping);

// Reads the filament types out of the print dialog's ams_mapping_info, one per filament, empty where the dialog gave none.
// Returns false when `ams_mapping_info` is not a JSON array.
bool parse_filament_types(const std::string& ams_mapping_info, std::vector<std::string>& types);

// Reads Moonraker's /printer/gcode/help answer. Returns false when it is not a JSON object of commands.
bool parse_box_temp_support(const std::string& gcode_help, bool& supported);

// Finds a map block entry that disagrees with the dialog's `mapping`: another slot, or none. Needs no printer state.
std::optional<Problem> find_map_conflict(const Blocks& blocks, const std::vector<int>& mapping);

// Finds the first problem the printer's live state raises. The dialog maps an unused filament to -1 just as it does an
// external spool, so used filaments are those with a type in `filament_types`; no `filament_types` means every filament is used.
// `filament_types` must be normalized like the slot types. A slot missing from `state.slots` is not checked, and no
// `filament_types` leaves the types unchecked.
std::optional<Problem> find_state_problem(const Blocks&                   blocks,
                                          const std::vector<int>&         mapping,
                                          const std::vector<std::string>& filament_types,
                                          const BoxState&                 state);

}} // namespace Slic3r::QidiBox
