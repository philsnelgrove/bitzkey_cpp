#include "ui/Sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <mutex>
#include <vector>

#include "core/Brain.hpp"

namespace bitzkey::ui {

namespace {

constexpr int RATE = 44100;
constexpr double LENGTH = 0.25;   // seconds
constexpr double PITCH = 1600.0;  // the beep's, in hertz
constexpr std::size_t VOICES = 12;  // sounding at once at most: the click, and the calls

SDL_AudioDeviceID device = 0;
std::vector<float> click;
std::array<std::vector<float>, CALL_OPTIONS> calls;  // each call's sound, none for silence
const SDL_Surface* frame = nullptr;
std::vector<Rect> drawing, shown;  // the buttons of the frame being drawn, and of the one last shown
std::mutex buttons;                // presses are heard as events arrive

// A sound being played: how far through it is, and how loud in each ear.
struct Voice {
    const std::vector<float>* samples;
    std::size_t at = 0;
    float left = 1.0f, right = 1.0f;
};
std::vector<Voice> voices;  // guarded by the device's lock, as the mixer reads them

void SDLCALL mix(void*, Uint8* stream, int bytes) {
    float* out = reinterpret_cast<float*>(stream);
    const std::size_t frames = static_cast<std::size_t>(bytes) / (2 * sizeof(float));
    std::fill(out, out + 2 * frames, 0.0f);
    for (Voice& voice : voices) {
        const std::vector<float>& samples = *voice.samples;
        for (std::size_t index = 0; index < frames && voice.at < samples.size(); ++index, ++voice.at) {
            out[2 * index] += samples[voice.at] * voice.left;
            out[2 * index + 1] += samples[voice.at] * voice.right;
        }
    }
    std::erase_if(voices, [](const Voice& voice) { return voice.at >= voice.samples->size(); });
    for (std::size_t index = 0; index < 2 * frames; ++index) out[index] = std::clamp(out[index], -1.0f, 1.0f);
}

int SDLCALL heard(void*, SDL_Event* event) {
    if (event->type != SDL_MOUSEBUTTONDOWN || event->button.button != SDL_BUTTON_LEFT || !device) return 0;
    {
        const std::lock_guard lock(buttons);
        const bool on_button = std::any_of(shown.begin(), shown.end(), [&](const Rect& button) {
            return button.contains(event->button.x, event->button.y);
        });
        if (!on_button) return 0;
    }
    SDL_LockAudioDevice(device);
    std::erase_if(voices, [](const Voice& voice) { return voice.samples == &click; });  // a quick second press starts it afresh
    voices.push_back({&click});
    SDL_UnlockAudioDevice(device);
    return 0;
}

// The calls' voices, each a few tenths of a second, rising in at once and fading to nothing so none pops: a frequency
// at each moment, a tone (pure, or with its harmonics), a beat, and an envelope.
double sweep(double from, double to, double share) { return from * std::pow(to / from, share); }

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

std::vector<float> call_sound(int call, int rate) {
    if (call <= Silent || call >= CALL_OPTIONS) return {};
    // The alert, a sharp double yelp; then the five: a rising chirp, a low hoot, a warbling trill, a croak, and a
    // falling whistle.
    constexpr std::array<double, CALL_OPTIONS> lengths = {0.0, 0.26, 0.14, 0.32, 0.26, 0.16, 0.22};
    const double length = lengths[static_cast<std::size_t>(call)];
    const int count = static_cast<int>(length * rate);
    std::vector<float> samples(static_cast<std::size_t>(count));
    double phase = 0.0;
    std::uint32_t noise = 0x2545F491u + static_cast<std::uint32_t>(call);
    for (int index = 0; index < count; ++index) {
        const double time = static_cast<double>(index) / rate, share = time / length;
        double pitch = 0.0, envelope = 1.0, harmonics = 0.0, rasp = 0.0;
        switch (call) {
            case AlertCall: {  // two quick falling yelps
                const double half = share < 0.5 ? share * 2 : share * 2 - 1;
                pitch = sweep(2200.0, 1300.0, half);
                envelope = std::sin(std::numbers::pi * half);
                harmonics = 0.25;
                break;
            }
            case FirstCall:  // a chirp, rising
                pitch = sweep(1200.0, 2600.0, share);
                envelope = std::sin(std::numbers::pi * share);
                break;
            case FirstCall + 1:  // a hoot, low and wavering
                pitch = 380.0 * (1 + 0.03 * std::sin(2 * std::numbers::pi * 6 * time));
                envelope = std::min(1.0, time / 0.04) * std::exp(-3 * share);
                harmonics = 0.15;
                break;
            case FirstCall + 2:  // a trill, warbling quickly
                pitch = 1800.0 * (1 + 0.06 * std::sin(2 * std::numbers::pi * 28 * time));
                envelope = std::sin(std::numbers::pi * share) * (0.55 + 0.45 * std::sin(2 * std::numbers::pi * 28 * time));
                break;
            case FirstCall + 3:  // a croak, rough and low
                pitch = sweep(260.0, 200.0, share);
                envelope = std::min(1.0, time / 0.01) * std::exp(-2.5 * share);
                harmonics = 0.6;
                rasp = 0.35;
                break;
            default:  // a whistle, falling
                pitch = sweep(2600.0, 1300.0, share);
                envelope = std::sin(std::numbers::pi * share);
                break;
        }
        phase += 2 * std::numbers::pi * pitch / rate;
        noise = noise * 1664525u + 1013904223u;
        const double hiss = static_cast<double>(noise >> 8) / (1u << 24) * 2 - 1;
        double tone = std::sin(phase) + harmonics * (std::sin(2 * phase) / 2 + std::sin(3 * phase) / 3);
        tone = tone / (1 + harmonics) * (1 - rasp) + rasp * hiss * std::abs(std::sin(phase));
        const double fade = std::min({1.0, time / 0.003, (length - time) / 0.01});
        samples[static_cast<std::size_t>(index)] = static_cast<float>(0.5 * tone * envelope * std::max(0.0, fade));
    }
    return samples;
}

void start_sounds() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec wanted{};
    wanted.freq = RATE;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 2;
    wanted.samples = 512;
    wanted.callback = mix;
    device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (!device) return;
    click = click_sound(RATE);
    for (int call = 0; call < CALL_OPTIONS; ++call) calls[static_cast<std::size_t>(call)] = call_sound(call, RATE);
    SDL_PauseAudioDevice(device, 0);
    SDL_AddEventWatch(heard, nullptr);
}

void stop_sounds() {
    if (!device) return;
    SDL_DelEventWatch(heard, nullptr);
    SDL_CloseAudioDevice(device);
    device = 0;
    voices.clear();
}

void calls_heard(std::span<const HeardCall> heard_calls) {
    if (!device || heard_calls.empty()) return;
    std::vector<HeardCall> loudest(heard_calls.begin(), heard_calls.end());
    std::sort(loudest.begin(), loudest.end(), [](const HeardCall& a, const HeardCall& b) { return a.loudness > b.loudness; });
    SDL_LockAudioDevice(device);
    for (const HeardCall& call : loudest) {
        if (voices.size() >= VOICES) break;
        if (call.call <= Silent || call.call >= CALL_OPTIONS || call.loudness <= 0) continue;
        const double pan = std::clamp(call.pan, -1.0, 1.0), loudness = std::min(1.0, call.loudness);
        // Panned with equal power, so a call sounds as loud wherever it comes from.
        const double angle = (pan + 1) * std::numbers::pi / 4;
        voices.push_back({&calls[static_cast<std::size_t>(call.call)], 0, static_cast<float>(loudness * std::cos(angle)),
                          static_cast<float>(loudness * std::sin(angle))});
    }
    SDL_UnlockAudioDevice(device);
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
