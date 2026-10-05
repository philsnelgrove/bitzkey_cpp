#pragma once
// Each species' icon, drawn from simple shapes so it scales to any size: a tree for plants, a leaf
// for grazers, an apple for omnivores and a drumstick for predators. Shapes are laid out on a
// square from -1 to 1 either way, then scaled to `half`, half the icon's width.

#include "core/Genome.hpp"
#include "ui/Canvas.hpp"

namespace bitzkey::ui {

void draw_icon(Canvas& canvas, Species species, int cx, int cy, double half, Color colour);

}  // namespace bitzkey::ui
