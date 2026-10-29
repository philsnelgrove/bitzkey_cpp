#pragma once
// The app's sounds, made here rather than read from files, mixed as they overlap.
//
// The sound of a button pressed: a quarter-second click and beep. Every button's label is drawn through
// Canvas::label, which notes where the button lies in the window's frame; a left press on any button of the frame
// last shown plays it, whichever screen is open.
//
// The Bitzkeys' calls: each of the five, and the alert, its own short voice, heard from the map when zoomed in close.

#include <span>
#include <vector>

#include <SDL.h>

#include "ui/Canvas.hpp"

namespace bitzkey::ui {

// Opens the sound and listens for presses; with no sound to be had, buttons and calls are silent.
void start_sounds();
void stop_sounds();
// The surface the window's frames are drawn on: only buttons drawn there are heard.
void window_frame(const SDL_Surface* frame);
// A button drawn on `target`, and the frame shown: its buttons are those a press is heard on, until the next.
void button_drawn(const SDL_Surface* target, const Rect& button);
void frame_shown();
// The click and beep, as samples at its rate, for the tests.
std::vector<float> click_sound(int rate);

// A call heard on the map: which (Brain.hpp's Call: the alert, or one of the five), how loud, 0 to 1, and where
// across the view it came from, -1 at its left edge to 1 at its right.
struct HeardCall {
    int call = 0;
    double loudness = 0.0;
    double pan = 0.0;
};
// Played at once, the loudest first, as many as can sound together.
void calls_heard(std::span<const HeardCall> calls);
// A call's sound, as samples at a rate, at full loudness; none for silence. For the tests.
std::vector<float> call_sound(int call, int rate);

}  // namespace bitzkey::ui
