#pragma once
// A Bitzkey's details, as the sidebar shows the selected one and the colour graph's tooltip shows
// the one under the mouse: its look, genome, temperament, body, condition and latest choice.

#include <array>
#include <functional>
#include <optional>

#include "core/Simulation.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

// Heights, in design pixels, of the details: in full, and for a Bitzkey known only by its genome.
constexpr int DETAILS_HEIGHT = 382;
constexpr int GENOME_HEIGHT = 334;
// Each temperature's colour, from hot to very cold, for the map's zones and the details.
constexpr std::array<Color, 4> TEMPERATURE_COLOURS = {{{238, 160, 110}, {175, 192, 172}, {140, 185, 230}, {200, 225, 250}}};

// Draw the details from x across `width`; `at` turns a design offset from their top into a screen
// y. A Bitzkey not `known` is one no longer (or not yet) in the world, rebuilt from its genome, so
// only what its genes fix is shown. `here` is the temperature where it is.
void draw_details(Canvas& canvas, const Bitzkey& agent, int x, const std::function<int(int)>& at, int width,
                  bool known = true, std::optional<double> here = std::nullopt);

// The details in a box beside the mouse, kept on the screen. Returns the box.
Rect draw_tooltip(Canvas& canvas, const Bitzkey& agent, bool known, int mouse_x, int mouse_y,
                  std::optional<double> here = std::nullopt);

}  // namespace bitzkey::ui
