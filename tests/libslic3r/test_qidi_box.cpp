#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "libslic3r/QidiBox.hpp"

using namespace Slic3r;

TEST_CASE("A slot line assigns the Box slot to the filament's value_t variable", "[QidiBox]")
{
    CHECK(QidiBox::format_slot_line(2, 5) == "SAVE_VARIABLE VARIABLE=value_t2 VALUE=\"'slot5'\"");
}

TEST_CASE("A slot line keeps multi-digit filament and slot numbers whole", "[QidiBox]")
{
    CHECK(QidiBox::format_slot_line(15, 12) == "SAVE_VARIABLE VARIABLE=value_t15 VALUE=\"'slot12'\"");
}

TEST_CASE("A slot line reads back as the filament and slot that wrote it", "[QidiBox]")
{
    int filament = -1;
    int slot     = -1;
    REQUIRE(QidiBox::parse_slot_line(QidiBox::format_slot_line(12, 7), filament, slot));
    CHECK(filament == 12);
    CHECK(slot == 7);
    CHECK_FALSE(QidiBox::parse_slot_line("SAVE_VARIABLE VARIABLE=enable_box VALUE=1", filament, slot));
    CHECK_FALSE(QidiBox::parse_slot_line("SAVE_VARIABLE VARIABLE=value_t1 VALUE=\"'slot'\"", filament, slot));
}

TEST_CASE("The blocks at the head of the G-code name their slots and temperature filaments", "[QidiBox]")
{
    const QidiBox::Blocks blocks = QidiBox::parse_blocks(
        "; header\n; QIDI_BOX_MAP_BEGIN\n" + QidiBox::format_slot_line(0, 2) + "\n" + QidiBox::format_slot_line(3, 1) +
        "\n; QIDI_BOX_MAP_END\n; QIDI_BOX_TEMP_BEGIN\nDISABLE_BOX_HEATER\nBOX_TEMP_SET VT0=45 VT3=0\n; QIDI_BOX_TEMP_END\n");

    CHECK(blocks.has_map);
    CHECK(blocks.map_slots == std::vector<std::pair<int, int>>{{0, 2}, {3, 1}});
    CHECK(blocks.has_temp);
    CHECK(blocks.temp_filaments == std::vector<int>{0, 3});
}

TEST_CASE("G-code without the blocks parses as having none", "[QidiBox]")
{
    // A start G-code may quote a marker mid-line
    const QidiBox::Blocks blocks = QidiBox::parse_blocks("; header\nM117 ; QIDI_BOX_MAP_BEGIN\nG1 X1\n; QIDI_BOX_MAP_END\n");

    CHECK_FALSE(blocks.has_map);
    CHECK_FALSE(blocks.has_temp);
}

TEST_CASE("A map block entry that disagrees with the dialog is a conflict", "[QidiBox]")
{
    QidiBox::Blocks blocks;
    blocks.has_map   = true;
    blocks.map_slots = {{0, 0}, {2, 2}};

    CHECK_FALSE(QidiBox::find_map_conflict(blocks, {0, -1, 2}));

    const auto other_slot = QidiBox::find_map_conflict(blocks, {2, -1, 0});
    REQUIRE(other_slot);
    CHECK(other_slot->kind == QidiBox::Problem::Kind::MapDisagrees);
    CHECK(other_slot->filament == 0);
    CHECK(other_slot->slot == 0);

    const auto no_slot = QidiBox::find_map_conflict(blocks, {0, -1, -1});
    REQUIRE(no_slot);
    CHECK(no_slot->filament == 2);
    CHECK(QidiBox::find_map_conflict(blocks, {0}));
}

TEST_CASE("A used filament mapped past the printer's Boxes is a problem", "[QidiBox]")
{
    const QidiBox::BoxState state{1, true};

    const auto problem = QidiBox::find_state_problem({}, {0, 5}, {}, state);
    REQUIRE(problem);
    CHECK(problem->kind == QidiBox::Problem::Kind::SlotBeyondBoxes);
    CHECK(problem->filament == 1);
    CHECK(problem->slot == 5);
    CHECK_FALSE(QidiBox::find_state_problem({}, {0, 3}, {}, state));
}

TEST_CASE("An unused filament is not checked against the printer's Boxes", "[QidiBox]")
{
    CHECK_FALSE(QidiBox::find_state_problem({}, {0, 9}, {"PLA", ""}, {1, true}));
    CHECK(QidiBox::find_state_problem({}, {0, 9}, {"PLA", "PETG"}, {1, true}));
}

TEST_CASE("A temperature for a filament past the printer's Boxes is a problem", "[QidiBox]")
{
    QidiBox::Blocks blocks;
    blocks.has_temp       = true;
    blocks.temp_filaments = {0, 4};

    const auto problem = QidiBox::find_state_problem(blocks, {0}, {}, {1, true});
    REQUIRE(problem);
    CHECK(problem->kind == QidiBox::Problem::Kind::TempBeyondBoxes);
    CHECK(problem->filament == 4);
    CHECK_FALSE(QidiBox::find_state_problem(blocks, {0}, {}, {2, true}));
}

TEST_CASE("A temperature block is a problem only when the firmware lacks the commands", "[QidiBox]")
{
    QidiBox::Blocks blocks;
    blocks.has_temp = true;

    const auto problem = QidiBox::find_state_problem(blocks, {0}, {}, {1, false});
    REQUIRE(problem);
    CHECK(problem->kind == QidiBox::Problem::Kind::CommandsMissing);
    CHECK_FALSE(QidiBox::find_state_problem({}, {0}, {}, {1, false}));
}

TEST_CASE("Moonraker's command list says whether the Box temperature commands exist", "[QidiBox]")
{
    bool supported = false;
    REQUIRE(QidiBox::parse_box_temp_support(R"({"result": {"G28": "Home", "BOX_TEMP_SET": "Set", "DISABLE_BOX_HEATER": "Off"}})", supported));
    CHECK(supported);
    REQUIRE(QidiBox::parse_box_temp_support(R"({"result": {"G28": "Home", "DISABLE_BOX_HEATER": "Off"}})", supported));
    CHECK_FALSE(supported);
    CHECK_FALSE(QidiBox::parse_box_temp_support("not json", supported));
}

TEST_CASE("A slot mapping reads as one slot per filament with -1 for none", "[QidiBox]")
{
    std::vector<int> mapping;
    REQUIRE(QidiBox::parse_slot_mapping("[2, -1, \"x\", 0]", mapping));
    CHECK(mapping == std::vector<int>{2, -1, -1, 0});
    CHECK_FALSE(QidiBox::parse_slot_mapping("{\"0\": 1}", mapping));
}

TEST_CASE("Filament types read from the dialog's mapping info, empty where it gave none", "[QidiBox]")
{
    std::vector<std::string> types;
    REQUIRE(QidiBox::parse_filament_types(R"([{"filamentType": "PLA"}, {"filamentType": ""}, {}, 3])", types));
    CHECK(types == std::vector<std::string>{"PLA", "", "", ""});
    CHECK_FALSE(QidiBox::parse_filament_types("{}", types));
}
