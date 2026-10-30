#include "QidiBox.hpp"

#include <charconv>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace QidiBox {

namespace {

// The lines between a block's markers. The begin marker must start a line, as a start G-code may quote it.
std::optional<std::string> block_body(const std::string& gcode, const std::string& name)
{
    const std::string begin_marker = "; QIDI_BOX_" + name + "_BEGIN\n";
    const std::string end_marker   = "; QIDI_BOX_" + name + "_END\n";

    size_t begin = gcode.find(begin_marker);
    while (begin != std::string::npos && begin != 0 && gcode[begin - 1] != '\n')
        begin = gcode.find(begin_marker, begin + 1);
    if (begin == std::string::npos)
        return std::nullopt;

    const size_t body = begin + begin_marker.size();
    const size_t end  = gcode.find(end_marker, body);
    if (end == std::string::npos)
        return std::nullopt;
    return gcode.substr(body, end - body);
}

bool consume(std::string_view& text, std::string_view prefix)
{
    if (text.substr(0, prefix.size()) != prefix)
        return false;
    text.remove_prefix(prefix.size());
    return true;
}

bool consume_int(std::string_view& text, int& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc())
        return false;
    text.remove_prefix(static_cast<size_t>(result.ptr - text.data()));
    return true;
}

} // namespace

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

bool parse_slot_line(const std::string& line, int& filament, int& slot)
{
    std::string_view text = line;
    return consume(text, "SAVE_VARIABLE VARIABLE=value_t") && consume_int(text, filament) && consume(text, " VALUE=\"'slot") &&
           consume_int(text, slot) && text == "'\"";
}

Blocks parse_blocks(const std::string& gcode)
{
    Blocks blocks;
    if (const auto map = block_body(gcode, "MAP")) {
        blocks.has_map = true;
        std::istringstream stream(*map);
        std::string        line;
        while (std::getline(stream, line)) {
            int filament = 0;
            int slot     = 0;
            if (parse_slot_line(line, filament, slot))
                blocks.map_slots.emplace_back(filament, slot);
        }
    }
    if (const auto temp = block_body(gcode, "TEMP")) {
        blocks.has_temp = true;
        std::istringstream stream(*temp);
        std::string        line;
        while (std::getline(stream, line)) {
            std::istringstream tokens(line);
            std::string        token;
            tokens >> token;
            if (token != "BOX_TEMP_SET")
                continue;
            while (tokens >> token) {
                std::string_view text = token;
                int              filament = 0;
                if (consume(text, "VT") && consume_int(text, filament) && consume(text, "="))
                    blocks.temp_filaments.push_back(filament);
            }
        }
    }
    return blocks;
}

bool parse_slot_mapping(const std::string& ams_mapping, std::vector<int>& mapping)
{
    const auto json = nlohmann::json::parse(ams_mapping, nullptr, false);
    if (!json.is_array())
        return false;
    mapping.clear();
    for (const auto& entry : json)
        mapping.push_back(entry.is_number_integer() ? entry.get<int>() : -1);
    return true;
}

bool parse_filament_types(const std::string& ams_mapping_info, std::vector<std::string>& types)
{
    const auto json = nlohmann::json::parse(ams_mapping_info, nullptr, false);
    if (!json.is_array())
        return false;
    types.clear();
    for (const auto& entry : json) {
        const bool has_type = entry.is_object() && entry.contains("filamentType") && entry["filamentType"].is_string();
        types.push_back(has_type ? entry["filamentType"].get<std::string>() : std::string());
    }
    return true;
}

bool parse_box_temp_support(const std::string& gcode_help, bool& supported)
{
    const auto json = nlohmann::json::parse(gcode_help, nullptr, false);
    if (!json.is_object() || !json.contains("result") || !json["result"].is_object())
        return false;
    const auto& commands = json["result"];
    supported            = commands.contains("BOX_TEMP_SET") && commands.contains("DISABLE_BOX_HEATER");
    return true;
}

std::optional<Problem> find_map_conflict(const Blocks& blocks, const std::vector<int>& mapping)
{
    for (const auto& [filament, slot] : blocks.map_slots)
        if (filament < 0 || static_cast<size_t>(filament) >= mapping.size() || mapping[filament] != slot)
            return Problem{Problem::Kind::MapDisagrees, filament, slot};
    return std::nullopt;
}

std::optional<Problem> find_state_problem(const Blocks&                   blocks,
                                          const std::vector<int>&         mapping,
                                          const std::vector<std::string>& filament_types,
                                          const BoxState&                 state)
{
    const int slot_limit = state.box_count * 4;
    for (size_t filament = 0; filament < mapping.size(); ++filament) {
        const bool used = filament_types.empty() || (filament < filament_types.size() && !filament_types[filament].empty());
        if (used && mapping[filament] >= slot_limit)
            return Problem{Problem::Kind::SlotBeyondBoxes, static_cast<int>(filament), mapping[filament]};
    }
    if (blocks.has_temp) {
        for (const int filament : blocks.temp_filaments)
            if (filament >= slot_limit)
                return Problem{Problem::Kind::TempBeyondBoxes, filament};
        if (!state.box_temp_supported)
            return Problem{Problem::Kind::CommandsMissing};
    }
    return std::nullopt;
}

}} // namespace Slic3r::QidiBox
