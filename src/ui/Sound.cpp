#include "ui/Sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <mutex>
#include <vector>

namespace bitzkey::ui {

namespace {

constexpr int RATE = 44100;
constexpr double LENGTH = 0.25;   // seconds
constexpr double PITCH = 1600.0;  // the beep's, in hertz

SDL_AudioDeviceID device = 0;
std::vector<float> sound;
const SDL_Surface* frame = nullptr;
std::vector<Rect> drawing, shown;  // the buttons of the frame being drawn, and of the one last shown
std::mutex buttons;                // presses are heard as events arrive

int SDLCALL heard(void*, SDL_Event* event) {
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT || !device) return 0;
    {
        const std::lock_guard lock(buttons);
        const bool on_button = std::any_of(shown.begin(), shown.end(), [&](const Rect& button) {
            return button.contains(event->button.x, event->button.y);
        });
        if (!on_button) return 0;
    }
    SDL_ClearQueuedAudio(device);  // a quick second press starts it afresh
    SDL_QueueAudio(device, sound.data(), static_cast<Uint32>(sound.size() * sizeof(float)));
    return 0;
}

}  // namespace

std::vector<float> click_sound(int rate) {
    // A click, a few milliseconds of a sharp falling edge with a little noise, under a beep that sounds at once and
    // dies away, the whole fading to nothing by its end so it doesn't pop.
    const int count = static_cast<int>(LENGTH * rate);
    std::vector<float> samples(static_cast<std::size_t>(count));
    std::uint32_t noise = 0x9E3779B9u;
    for (int index = 0; index < count; ++index) {
        const double time = static_cast<double>(index) / rate;
        noise = noise * 1664525u + 1013904223u;
        const double hiss = static_cast<double>(noise >> 8) / (1u << 24) * 2 - 1;
        const double click = std::exp(-time / 0.0015) * (0.7 + 0.3 * hiss);
        const double attack = std::min(1.0, time / 0.002);
        const double beep = attack * std::exp(-time / 0.045) * std::sin(2 * std::numbers::pi * PITCH * time);
        const double fade = std::min(1.0, (LENGTH - time) / 0.02);
        samples[static_cast<std::size_t>(index)] = static_cast<float>(0.3 * (0.6 * click + beep) * fade);
    }
    return samples;
}

void start_sounds() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec wanted{};
    wanted.freq = RATE;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 1;
    wanted.samples = 512;
    device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (!device) return;
    sound = click_sound(RATE);
    SDL_PauseAudioDevice(device, 0);
    SDL_AddEventWatch(heard, nullptr);
}

void stop_sounds() {
    if (!device) return;
    SDL_DelEventWatch(heard, nullptr);
    SDL_CloseAudioDevice(device);
    device = 0;
}

void window_frame(const SDL_Surface* surface) { frame = surface; }

void button_drawn(const SDL_Surface* target, const Rect& button) {
    if (target == frame && frame) drawing.push_back(button);
}

void frame_shown() {
    const std::lock_guard lock(buttons);
    shown.swap(drawing);
    drawing.clear();
}

}  // namespace bitzkey::ui
