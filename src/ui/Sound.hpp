#pragma once
// The sound of a button pressed: a quarter-second click and beep, made here rather than read from a file. Every
// button's label is drawn through Canvas::label, which notes where the button lies in the window's frame; a left
// press on any button of the frame last shown plays it, whichever screen is open.

#include <vector>

#include <SDL.h>

#include "ui/Canvas.hpp"

namespace bitzkey::ui {

// Opens the sound and listens for presses; with no sound to be had, buttons are silent.
void start_sounds();
void stop_sounds();
// The surface the window's frames are drawn on: only buttons drawn there are heard.
void window_frame(const SDL_Surface* frame);
// A button drawn on `target`, and the frame shown: its buttons are those a press is heard on, until the next.
void button_drawn(const SDL_Surface* target, const Rect& button);
void frame_shown();
// The click and beep, as samples at its rate, for the tests.
std::vector<float> click_sound(int rate);

}  // namespace bitzkey::ui
