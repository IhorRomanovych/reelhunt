// Game/game_text.cpp - on-screen game text. See game_text.h.
#include "Game/game_text.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace Game {

namespace {

constexpr UiColor kWhite{230, 230, 230, 255};
constexpr float32 kBoxScale = 0.5f;      // font 1 is 31 px: lines 0.030 tall in the box
constexpr float32 kSubtitleScale = 0.5f;
constexpr float32 kSubtitleY = 0.86f;     // top of the subtitle block
constexpr float32 kSubtitleWidth = 0.84f;
constexpr float32 kPad = 0.012f;
constexpr float32 kTeletypeRate = 60.0f;  // characters per second (ours)

UiColor tokenColor(const std::string& token, bool& known)
{
    known = true;
    if (token == "green") return {60, 220, 60, 255};
    if (token == "red") return {235, 40, 40, 255};
    if (token == "orange") return {255, 150, 30, 255};
    if (token == "yellow") return {250, 230, 40, 255};
    if (token == "blue") return {70, 120, 255, 255};
    if (token == "colour" || token == "colur" || token == "color") return kWhite;
    known = false;
    return kWhite;
}

} // namespace

bool GameText::load(const std::string& levelDir, const std::string& levelName, const std::string& suffix)
{
    std::string lower = levelName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // pc_<level><suffix>.gxt in the chosen language (_g _f _i _s), else the English one.
    auto gxt = Assets::Gxt::load(levelDir + "pc_text/pc_" + lower + suffix + ".gxt");
    if (!gxt && !suffix.empty())
        gxt = Assets::Gxt::load(levelDir + "pc_text/pc_" + lower + ".gxt");
    if (!gxt)
    {
        std::cerr << "text: " << gxt.error().message << "\n";
        return false;
    }
    gxt_ = std::move(*gxt);
    loaded_ = true;
    return true;
}

std::string GameText::text(const std::string& key) const
{
    return loaded_ ? gxt_.text(key) : key;
}

void GameText::display(const std::string& key)
{
    boxKey_ = key;
    boxText_ = text(key);
    boxLeft_ = displaySeconds_;
    boxShown_ = 0.0f;
}

void GameText::displayText(const std::string& key, const std::string& text)
{
    boxKey_ = key;
    boxText_ = text;
    boxLeft_ = displaySeconds_;
    boxShown_ = 0.0f;
}

bool GameText::subtitle(const std::string& key, float32 seconds)
{
    if (!loaded_ || !gxt_.contains(key))
        return false;
    subtitleKey_ = key;
    subtitleText_ = gxt_.text(key);
    subtitleLeft_ = seconds;
    return true;
}

void GameText::setGoal(const std::string& key)
{
    if (std::find(goals_.begin(), goals_.end(), key) == goals_.end())
        goals_.push_back(key);
}

void GameText::clearGoal(const std::string& key)
{
    goals_.erase(std::remove(goals_.begin(), goals_.end(), key), goals_.end());
}

void GameText::update(float32 dt)
{
    boxLeft_ = std::max(0.0f, boxLeft_ - dt);
    boxShown_ += dt;
    subtitleLeft_ = std::max(0.0f, subtitleLeft_ - dt);
}

std::vector<GameText::Run> GameText::parse(const std::string& s)
{
    std::vector<Run> runs;
    UiColor color = kWhite;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty())
            runs.push_back({cur, color});
        cur.clear();
    };
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '~')
        {
            const std::size_t end = s.find('~', i + 1);
            if (end != std::string::npos)
            {
                std::string token = s.substr(i + 1, end - i - 1);
                std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                bool known = false;
                const UiColor c = tokenColor(token, known);
                if (known)
                {
                    flush();
                    color = c;
                }
                i = end;
                continue;
            }
        }
        cur += s[i];
    }
    flush();
    return runs;
}

std::vector<std::vector<GameText::Run>> GameText::wrap(const std::vector<Run>& runs, const UiText& font, float32 scale, float32 width,
                                                       std::size_t maxLines)
{
    // Split into words that keep their colour (a word never spans two colours here: tokens sit
    // at word boundaries in the game's text).
    struct Word {
        std::string text;
        UiColor color;
    };
    std::vector<Word> words;
    for (const Run& r : runs)
    {
        std::size_t i = 0;
        while (i < r.text.size())
        {
            while (i < r.text.size() && r.text[i] == ' ')
                ++i;
            std::size_t j = i;
            while (j < r.text.size() && r.text[j] != ' ')
                ++j;
            if (j > i)
                words.push_back({r.text.substr(i, j - i), r.color});
            i = j;
        }
    }
    std::vector<std::vector<Run>> lines(1);
    float32 x = 0.0f;
    const float32 space = font.width(" ", scale);
    for (const Word& w : words)
    {
        const float32 ww = font.width(w.text, scale);
        if (x > 0.0f && x + space + ww > width && (maxLines == 0 || lines.size() < maxLines))
        {
            lines.emplace_back();
            x = 0.0f;
        }
        auto& line = lines.back();
        const std::string text = x > 0.0f ? " " + w.text : w.text;
        if (!line.empty() && line.back().color.r == w.color.r && line.back().color.g == w.color.g && line.back().color.b == w.color.b)
            line.back().text += text;
        else
            line.push_back({text, w.color});
        x += (x > 0.0f ? space : 0.0f) + ww;
    }
    return lines;
}

void GameText::draw(Renderer::UiBatch& ui, const UiText& font) const
{
    if (!font.valid())
        return;
    auto drawLines = [&](const std::vector<std::vector<Run>>& lines, float32 x, float32 y, float32 scale, bool centred,
                         float32 centreWidth, std::size_t maxChars) {
        const float32 lh = font.lineHeight(scale);
        std::size_t left = maxChars;
        for (const auto& line : lines)
        {
            float32 lx = x;
            if (centred)
            {
                float32 w = 0.0f;
                for (const Run& r : line)
                    w += font.width(r.text, scale);
                lx = x + (centreWidth - w) * 0.5f;
            }
            for (const Run& r : line)
            {
                if (left == 0)
                    return;
                const std::string t = r.text.substr(0, std::min(left, r.text.size()));
                left -= t.size();
                font.draw(ui, t, lx + 0.002f, y + 0.002f, scale, UiColor{0, 0, 0, 200}); // drop shadow
                font.draw(ui, t, lx, y, scale, r.color);
                lx += font.width(r.text, scale);
            }
            y += lh;
        }
    };

    if (displaying())
    {
        const auto lines = wrap(parse(boxText_), font, kBoxScale, boxW_ - 2.0f * kPad);
        const float32 lh = font.lineHeight(kBoxScale);
        const float32 h = std::min(boxH_, lh * static_cast<float32>(lines.size()) + 2.0f * kPad);
        ui.rect(boxX_, boxY_, boxX_ + boxW_, boxY_ + h, 0, 0, 0, 150);
        std::size_t chars = static_cast<std::size_t>(-1);
        if (teletype_)
            chars = static_cast<std::size_t>(boxShown_ * kTeletypeRate);
        drawLines(lines, boxX_ + kPad, boxY_ + kPad, kBoxScale, false, 0.0f, chars);
    }
    if (subtitleLeft_ > 0.0f)
    {
        const auto lines = wrap(parse(subtitleText_), font, kSubtitleScale, kSubtitleWidth);
        const float32 x = (1.0f - kSubtitleWidth) * 0.5f;
        drawLines(lines, x, kSubtitleY, kSubtitleScale, true, kSubtitleWidth, static_cast<std::size_t>(-1));
    }
}

} // namespace Game
