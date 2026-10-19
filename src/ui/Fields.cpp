#include "ui/Fields.hpp"

#include <algorithm>
#include <cctype>

#include "ui/Language.hpp"

namespace bitzkey::ui {

namespace {

constexpr std::uint64_t MB = 1 << 20;
constexpr Color INK{220, 229, 211};
constexpr Color FAINT{130, 152, 136};
constexpr Color LABEL{175, 192, 172};

}  // namespace

void TextField::draw(Canvas& canvas, bool focused) const {
    canvas.rect(rect, {19, 26, 27}, 0, UI(4));
    canvas.rect(rect, focused ? Color{131, 205, 143} : Color{54, 68, 62}, 1, UI(4));
    TTF_Font* font = UI.font(Face::Mono, 14);
    const int middle = rect.centery();
    canvas.set_clip(rect.inflate(-UI(4), 0));
    if (text.empty() && !focused) {
        canvas.text_at(UI.font(Face::Sans, 13), placeholder, FAINT, [&](Rect& box) { box.midleft_at(rect.x + UI(8), middle); });
    } else {
        const std::string shown = text + (focused && (SDL_GetTicks() / 500) % 2 ? "|" : "");
        const std::string& as_typed = as_shown(shown);
        // The end in view, as it is typed, when it outgrows the box.
        const int width = Canvas::text_width(font, as_typed);
        const int left = std::min(rect.x + UI(8), rect.right() - UI(8) - width);
        canvas.text_at(font, as_typed, {238, 238, 220}, [&](Rect& box) { box.midleft_at(left, middle); });
    }
    canvas.set_clip(std::nullopt);
}

void TextField::type(std::string_view typed) {
    for (const char character : typed) {
        if (text.size() >= max) break;
        if (digits && !std::isdigit(static_cast<unsigned char>(character))) continue;
        if (static_cast<unsigned char>(character) < 32) continue;
        text += character;
    }
}

void TextField::backspace() { pop_character(text); }

std::optional<int> TextField::number() const {
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return std::nullopt;
    }
    return std::stoi(text);
}

void draw_button(Canvas& canvas, const Rect& rect, std::string_view label, Tone tone) {
    const Color fill = tone == Tone::Primary ? Color{131, 205, 143} : tone == Tone::Warning ? Color{150, 70, 60} : Color{44, 57, 52};
    const Color ink = tone == Tone::Primary ? Color{19, 26, 27} : Color{236, 238, 232};
    canvas.rect(rect, fill, 0, UI(6));
    canvas.label(UI.font(Face::Sans, 13, true), label, ink, rect);
}

ShareForm::ShareForm(const net::Machine& machine, const net::Resources& shared) : machine_(machine) {
    cores_.digits = memory_.digits = true;
    cores_.max = 4;
    memory_.max = 8;
    cores_.text = std::to_string(std::max(1, shared.threads));
    memory_.text = std::to_string(std::max<std::uint64_t>(64, shared.memory / MB));
    gpus_.assign(machine.gpus.size(), false);
    for (const int index : shared.gpus) {
        if (index >= 0 && static_cast<std::size_t>(index) < gpus_.size()) gpus_[static_cast<std::size_t>(index)] = true;
    }
}

int ShareForm::draw(Canvas& canvas, int x, int y, int width, const TextField* focused) {
    (void)width;
    TTF_Font* label = UI.font(Face::Sans, 13);
    TTF_Font* small = UI.font(Face::Sans, 12);
    const int box_left = x + UI(130), row = UI(38);
    int at = y;
    const auto line = [&](const char* name, TextField& field, const std::string& hint) {
        canvas.text_at(label, name, LABEL, [&](Rect& box) { box.midleft_at(x, at + row / 2); });
        field.rect = {box_left, at + UI(4), UI(100), UI(30)};
        field.draw(canvas, focused == &field);
        canvas.text_at(small, hint, FAINT, [&](Rect& box) { box.midleft_at(field.rect.right() + UI(10), at + row / 2); });
        at += row;
    };
    line("Cores", cores_, "of " + std::to_string(machine_.threads));
    line("Memory (MB)", memory_, "of " + std::to_string(machine_.memory / MB) + " MB, for brains");
    ticks_.clear();
    if (machine_.gpus.empty()) {
        canvas.text(small, "No GPU found: it thinks on the CPU.", FAINT, x, at + UI(6));
        return at + UI(30) - y;
    }
    canvas.text_at(label, "GPUs", LABEL, [&](Rect& box) { box.midleft_at(x, at + UI(14)); });
    for (std::size_t index = 0; index < machine_.gpus.size(); ++index) {
        const Rect tick{box_left, at + UI(5), UI(18), UI(18)};
        canvas.rect(tick, {19, 26, 27}, 0, UI(3));
        canvas.rect(tick, {120, 140, 126}, 1, UI(3));
        if (gpus_[index]) canvas.rect(tick.inflate(-UI(8), -UI(8)), {131, 205, 143}, 0, UI(2));
        const gpu::DeviceInfo& device = machine_.gpus[index];
        const int name = canvas.text(label, as_shown(device.name), INK, tick.right() + UI(10), at + UI(5));
        canvas.text(small, std::to_string(device.memory / MB) + " MB", FAINT, tick.right() + UI(20) + name, at + UI(7));
        ticks_.push_back({tick.x, tick.y, UI(28) + name, tick.h});
        at += UI(28);
    }
    return at + UI(6) - y;
}

TextField* ShareForm::click(int x, int y) {
    if (cores_.rect.contains(x, y)) return &cores_;
    if (memory_.rect.contains(x, y)) return &memory_;
    for (std::size_t index = 0; index < ticks_.size(); ++index) {
        if (ticks_[index].contains(x, y)) gpus_[index] = !gpus_[index];
    }
    return nullptr;
}

std::optional<net::Resources> ShareForm::read(std::string& problem) const {
    net::Resources shared;
    const auto cores = cores_.number(), memory = memory_.number();
    if (!cores || *cores < 1 || *cores > machine_.threads) {
        problem = "Share between 1 and " + std::to_string(machine_.threads) + " cores.";
        return std::nullopt;
    }
    if (!memory || *memory < 64) {
        problem = "Share at least 64 MB of memory.";
        return std::nullopt;
    }
    shared.threads = *cores;
    shared.memory = static_cast<std::uint64_t>(*memory) * MB;
    for (std::size_t index = 0; index < gpus_.size(); ++index) {
        if (gpus_[index]) shared.gpus.push_back(static_cast<int>(index));
    }
    return shared;
}

}  // namespace bitzkey::ui
