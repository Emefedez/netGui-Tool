#include "rgl_layout.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "raygui.h"

namespace rgl {

/**
 * @brief Compute the same transform used for drawing.
 *
 * Strategy:
 * - Apply DPI scaling (HiDPI correctness).
 * - Compute the bounding box of all controls.
 * - Uniformly scale the whole layout to fit the current screen (can upscale).
 * - Center it on screen.
 */
static void computeTransform(const std::vector<Control>& controls, float& sx, float& sy, float& dx, float& dy)
{
    const Vector2 dpi = GetWindowScaleDPI();
    sx = (dpi.x > 0.0f) ? dpi.x : 1.0f;
    sy = (dpi.y > 0.0f) ? dpi.y : 1.0f;

    dx = 0.0f;
    dy = 0.0f;

    // Compute the bounding box of all controls (after DPI scaling).
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    bool hasAny = false;
    for (const auto& c : controls)
    {
        Rectangle r = c.rect;
        r.x *= sx;
        r.y *= sy;
        r.width *= sx;
        r.height *= sy;

        const float rMinX = r.x;
        const float rMinY = r.y;
        const float rMaxX = r.x + r.width;
        const float rMaxY = r.y + r.height;

        if (!hasAny)
        {
            minX = rMinX;
            minY = rMinY;
            maxX = rMaxX;
            maxY = rMaxY;
            hasAny = true;
        }
        else
        {
            minX = std::min(minX, rMinX);
            minY = std::min(minY, rMinY);
            maxX = std::max(maxX, rMaxX);
            maxY = std::max(maxY, rMaxY);
        }
    }

    if (!hasAny) return;

    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    const float margin = 24.0f;

    const float bboxW = std::max(1.0f, maxX - minX);
    const float bboxH = std::max(1.0f, maxY - minY);
    const float availW = std::max(1.0f, screenW - 2.0f * margin);
    const float availH = std::max(1.0f, screenH - 2.0f * margin);

    // Uniformly scale layout to fit current screen (allow upscale).
    const float fit = std::min(availW / bboxW, availH / bboxH);
    const float maxUpscale = 1.75f; // avoid absurdly huge UI on very large monitors
    const float finalScale = std::max(0.25f, std::min(fit, maxUpscale));

    sx *= finalScale;
    sy *= finalScale;

    // Center the scaled bounding box.
    dx = (screenW - (bboxW * finalScale)) * 0.5f - (minX * finalScale);
    dy = (screenH - (bboxH * finalScale)) * 0.5f - (minY * finalScale);
}

/**
 * @brief Trim left whitespace from a string.
 */
static std::string trimLeft(std::string s)
{
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    s.erase(0, i);
    return s;
}

void UiState::reset()
{
    editMode.clear();
    intValue.clear();
    text.clear();
}

/**
 * @brief Load and parse controls from a .rgl file.
 */
bool Layout::loadFromFile(const std::string& filePath)
{
    filePath_ = filePath;
    controls_.clear();

    std::ifstream in(filePath);
    if (!in.is_open()) return false;

    // Track last modification time for hot-reload.
    try
    {
        lastWriteTime_ = std::filesystem::last_write_time(std::filesystem::path(filePath));
    }
    catch (...) {}

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;
        if (line[0] == '#') continue;

        // Controls have the format:
        // c <id> <type> <name> <x> <y> <w> <h> <anchor_id> <text...>
        if (line.size() >= 2 && line[0] == 'c' && std::isspace(static_cast<unsigned char>(line[1])))
        {
            std::istringstream iss(line);
            char c;
            Control control;
            iss >> c >> control.id >> control.type >> control.name;
            iss >> control.rect.x >> control.rect.y >> control.rect.width >> control.rect.height;
            iss >> control.anchorId;

            std::string rest;
            std::getline(iss, rest);
            control.text = trimLeft(rest);

            controls_.push_back(std::move(control));
        }
    }

    return !controls_.empty();
}

bool Layout::reloadIfChanged()
{
    if (filePath_.empty()) return false;

    std::filesystem::file_time_type now{};
    try
    {
        now = std::filesystem::last_write_time(std::filesystem::path(filePath_));
    }
    catch (...)
    {
        return false;
    }

    if (now == lastWriteTime_) return false;

    // Reload (also updates lastWriteTime_).
    return loadFromFile(filePath_);
}

/**
 * @brief Check whether a control was pressed during the last draw().
 */
bool Layout::pressed(const std::string& controlName) const
{
    auto it = pressed_.find(controlName);
    return (it != pressed_.end()) ? it->second : false;
}

/**
 * @brief Look up a control rectangle by name.
 */
bool Layout::rectOf(const std::string& controlName, Rectangle& out) const
{
    float sx = 1.0f, sy = 1.0f, dx = 0.0f, dy = 0.0f;
    computeTransform(controls_, sx, sy, dx, dy);

    for (const auto& c : controls_)
    {
        if (c.name == controlName)
        {
            out = c.rect;
            out.x *= sx;
            out.y *= sy;
            out.width *= sx;
            out.height *= sy;
            out.x += dx;
            out.y += dy;
            return true;
        }
    }
    return false;
}

/**
 * @brief Draw all supported controls.
 */
void Layout::draw(UiState& state)
{
    pressed_.clear();

    float sx = 1.0f, sy = 1.0f, dx = 0.0f, dy = 0.0f;
    computeTransform(controls_, sx, sy, dx, dy);

    auto controlText = [&](const Control& c) -> const char* {
        auto it = state.text.find(c.name);
        if (it != state.text.end()) return it->second.c_str();
        return c.text.empty() ? nullptr : c.text.c_str();
    };

    for (const auto& c : controls_)
    {
        int result = 0;
        Rectangle bounds = c.rect;
        bounds.x *= sx;
        bounds.y *= sy;
        bounds.width *= sx;
        bounds.height *= sy;
        bounds.x += dx;
        bounds.y += dy;

        switch (c.type)
        {
            case 0: // WindowBox
                result = GuiWindowBox(bounds, controlText(c));
                pressed_[c.name] = (result != 0);
                break;
            case 2: // Line
                GuiLine(bounds, controlText(c));
                break;
            case 3: // Panel
                GuiPanel(bounds, controlText(c));
                break;
            case 5: // Button
                result = GuiButton(bounds, controlText(c));
                pressed_[c.name] = (result != 0);
                break;
            case 15: // Spinner
            {
                int& v = state.intValue[c.name];
                bool& edit = state.editMode[c.name];
                result = GuiSpinner(bounds, controlText(c), &v, 0, 1, edit);
                if (result != 0) edit = !edit;
                break;
            }
            case 19: // StatusBar
                GuiStatusBar(bounds, controlText(c));
                break;
            default:
                // Unknown control type: ignore.
                break;
        }
    }
}

}  // namespace rgl
