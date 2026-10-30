#include <catch2/catch_test_macros.hpp>

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
