#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ios>
#include <string>

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/path.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>

#include "libslic3r/QidiBox.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "slic3r/Utils/QidiPrinterAgent.hpp"
#include "slic3r/Utils/bambu_networking.hpp"
#include "test_utils.hpp"

using namespace Slic3r;

namespace {

// Answers the printer queries from canned state and counts them
class CannedQidiAgent : public QidiPrinterAgent
{
public:
    CannedQidiAgent() : QidiPrinterAgent("") {}

    using QidiPrinterAgent::check_upload;

    QidiBox::BoxState state{1, true};
    bool              query_succeeds = true;
    int               queries        = 0;

protected:
    bool query_box_state(QidiBox::BoxState& out, std::string& error) override
    {
        ++queries;
        out = state;
        if (!query_succeeds)
            error = "timed out";
        return query_succeeds;
    }
};

const std::string identity_map_gcode = "; header\n; QIDI_BOX_MAP_BEGIN\n" + QidiBox::format_slot_line(0, 0) + "\n" +
                                       QidiBox::format_slot_line(1, 1) + "\n; QIDI_BOX_MAP_END\n; QIDI_BOX_TEMP_BEGIN\n"
                                       "DISABLE_BOX_HEATER\nBOX_TEMP_SET VT0=45 VT1=0\n; QIDI_BOX_TEMP_END\nM104 S0\n";

void write_text(const boost::filesystem::path& path, const std::string& text)
{
    boost::filesystem::ofstream out(path, std::ios::binary);
    out << text;
}

void write_bundle(const boost::filesystem::path& path, int plate, const std::string& gcode)
{
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    REQUIRE(open_zip_writer(&zip, path.string()));
    REQUIRE(mz_zip_writer_add_mem(&zip, ("Metadata/plate_" + std::to_string(plate) + ".gcode").c_str(), gcode.data(), gcode.size(),
                                  MZ_DEFAULT_COMPRESSION));
    REQUIRE(mz_zip_writer_finalize_archive(&zip));
    REQUIRE(close_zip_writer(&zip));
}

PrintParams params_with(const std::string& ams_mapping)
{
    PrintParams params{};
    params.plate_index = 1;
    params.ams_mapping = ams_mapping;
    return params;
}

} // namespace

TEST_CASE("A filament mapped to a Box slot saves that slot in its tool variable", "[QidiPrinterAgent]")
{
    // Filament 2 is mapped to slot 5; filaments 0 and 1 have no slot.
    // SAVE_VARIABLE reads VALUE as a Python literal, so the slot name needs its own quotes.
    CHECK(QidiPrinterAgent::build_tool_slot_script("[-1, -1, 5]") == "SAVE_VARIABLE VARIABLE=value_t2 VALUE=\"'slot5'\"");
}

TEST_CASE("Each mapped filament gets its own tool variable line", "[QidiPrinterAgent]")
{
    CHECK(QidiPrinterAgent::build_tool_slot_script("[3, -1, 0]") ==
          "SAVE_VARIABLE VARIABLE=value_t0 VALUE=\"'slot3'\"\n"
          "SAVE_VARIABLE VARIABLE=value_t2 VALUE=\"'slot0'\"");
}

TEST_CASE("A mapping without a usable Box slot saves nothing", "[QidiPrinterAgent]")
{
    const std::string mapping = GENERATE(as<std::string>{},
                                         "",           // empty mapping
                                         "[]",         // no filaments
                                         "[-1, -1]",   // no filament has a slot
                                         "[16]",       // past the firmware's slot15
                                         "[\"slot1\"]", // not a slot index
                                         "{\"0\": 1}", // not an array
                                         "[1,");       // not valid JSON
    CAPTURE(mapping);
    CHECK(QidiPrinterAgent::build_tool_slot_script(mapping).empty());
}

TEST_CASE("Filaments past the firmware's last tool variable are skipped", "[QidiPrinterAgent]")
{
    // The firmware has value_t0 to value_t15: filament 15 maps to slot 1, filament 16 to slot 4.
    std::string mapping = "[";
    for (int filament = 0; filament < 15; ++filament)
        mapping += "-1, ";
    mapping += "1, 4]";

    CHECK(QidiPrinterAgent::build_tool_slot_script(mapping) == "SAVE_VARIABLE VARIABLE=value_t15 VALUE=\"'slot1'\"");
}

TEST_CASE("The print start of a 3MF names the plate to print", "[QidiPrinterAgent]")
{
    PrintParams params;
    params.plate_index = 2;

    CHECK(QidiPrinterAgent::print_start_request(params, "Cube.gcode.3mf") ==
          nlohmann::json{{"filename", "Cube.gcode.3mf"}, {"plateindex", "2"}});
    CHECK(QidiPrinterAgent::print_start_request(params, "Cube.gcode") == nlohmann::json{{"filename", "Cube.gcode"}});
}

TEST_CASE("A map block that disagrees with the print dialog is refused before the printer is queried", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, identity_map_gcode);

    CannedQidiAgent agent;
    std::string     error;
    CHECK_FALSE(agent.check_upload(params_with("[1, 0]"), gcode.string(), error));
    CHECK_FALSE(error.empty());
    CHECK(agent.queries == 0);
}

TEST_CASE("A bundle's plate G-code is checked like plain G-code", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         bundle = dir.path() / "Cube.gcode.3mf";
    write_bundle(bundle, 1, identity_map_gcode);

    CannedQidiAgent agent;
    std::string     error;
    CHECK_FALSE(agent.check_upload(params_with("[1, 0]"), bundle.string(), error));
    CHECK_FALSE(error.empty());
    CHECK(agent.queries == 0);

    error.clear();
    CHECK(agent.check_upload(params_with("[0, 1]"), bundle.string(), error));
    CHECK(error.empty());
}

TEST_CASE("G-code without the Box blocks passes without querying the printer", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, "; header\nG28\n");

    CannedQidiAgent agent;
    std::string     error;
    CHECK(agent.check_upload(params_with("[7, 9]"), gcode.string(), error));
    CHECK(agent.queries == 0);
}

TEST_CASE("A map block that agrees with the dialog passes the live checks", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, identity_map_gcode);

    CannedQidiAgent agent;
    std::string     error;
    CHECK(agent.check_upload(params_with("[0, 1]"), gcode.string(), error));
    CHECK(agent.queries == 1);
}

TEST_CASE("A slot beyond the printer's Box count is refused", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, "; header\n; QIDI_BOX_MAP_BEGIN\n; QIDI_BOX_MAP_END\n; QIDI_BOX_TEMP_BEGIN\nDISABLE_BOX_HEATER\n; QIDI_BOX_TEMP_END\n");

    CannedQidiAgent agent;
    std::string     error;
    CHECK_FALSE(agent.check_upload(params_with("[0, 5]"), gcode.string(), error));
    CHECK(error.find("slot 6") != std::string::npos);
    CHECK(agent.queries == 1);
}

TEST_CASE("A temperature for a filament beyond the printer's Box count is refused", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, "; QIDI_BOX_TEMP_BEGIN\nDISABLE_BOX_HEATER\nBOX_TEMP_SET VT0=0 VT4=45\n; QIDI_BOX_TEMP_END\n");

    CannedQidiAgent agent;
    std::string     error;
    CHECK_FALSE(agent.check_upload(params_with("[0]"), gcode.string(), error));
    CHECK(error.find("filament 5") != std::string::npos);
}

TEST_CASE("Firmware without the Box temperature commands is refused", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, identity_map_gcode);

    CannedQidiAgent agent;
    agent.state.box_temp_supported = false;
    std::string error;
    CHECK_FALSE(agent.check_upload(params_with("[0, 1]"), gcode.string(), error));
    CHECK(error.find("BOX_TEMP_SET") != std::string::npos);
    CHECK(error.find("support_box_temp_control") != std::string::npos);
}

TEST_CASE("Without a dialog mapping the map block is accepted and the firmware is still checked", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, identity_map_gcode);

    CannedQidiAgent agent;
    std::string     error;
    CHECK(agent.check_upload(params_with(""), gcode.string(), error));

    agent.state.box_temp_supported = false;
    CHECK_FALSE(agent.check_upload(params_with(""), gcode.string(), error));
    CHECK(error.find("BOX_TEMP_SET") != std::string::npos);
}

TEST_CASE("A failed printer query lets the G-code upload as sliced", "[QidiPrinterAgent]")
{
    ScopedTemporaryDir dir("qidi-check");
    const auto         gcode = dir.path() / "Cube.gcode";
    write_text(gcode, identity_map_gcode);

    CannedQidiAgent agent;
    agent.query_succeeds           = false;
    agent.state.box_temp_supported = false;
    std::string error;
    CHECK(agent.check_upload(params_with("[0, 1]"), gcode.string(), error));
    CHECK(agent.queries == 1);
}
