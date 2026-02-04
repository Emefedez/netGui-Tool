#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "raylib.h"

/**
 * @brief Minimal loader/drawer for raygui .rgl layout files (rGuiLayout).
 *
 * The repository stores layouts as text .rgl files under layouts_netGui/.
 * This loader reads a subset of rgl fields (controls + rectangles + labels)
 * and renders them using raygui at runtime.
 *
 * Supported control types (as used by this project layouts):
 * - 0  WindowBox
 * - 2  Line
 * - 3  Panel
 * - 5  Button
 * - 15 Spinner
 * - 19 StatusBar
 */
namespace rgl {

/**
 * @brief One control entry parsed from an .rgl file.
 */
struct Control {
    int id = -1;
    int type = -1;
    std::string name;
    Rectangle rect{};
    int anchorId = 0;
    std::string text;
};

/**
 * @brief Per-layout mutable UI state for controls that require it.
 */
struct UiState {
    std::unordered_map<std::string, bool> editMode;
    std::unordered_map<std::string, int> intValue;
    std::unordered_map<std::string, std::string> text;

    /**
     * @brief Clear all cached state.
     */
    void reset();
};

/**
 * @brief Runtime representation of an .rgl layout.
 */
class Layout {
public:
    /**
     * @brief Load and parse a layout file.
     * @return true if parsing succeeded.
     */
    bool loadFromFile(const std::string& filePath);

    /**
     * @brief Reload the layout if the on-disk file changed since last load.
     * @return true if a reload happened.
     */
    bool reloadIfChanged();

    /** @brief Absolute/relative path used to load this layout. */
    const std::string& filePath() const { return filePath_; }

    /**
     * @brief Draw controls and capture button presses.
     *
     * After drawing, call pressed("ControlName") to check if a button-like
     * control was clicked in the last draw.
     */
    void draw(UiState& state);

    /**
     * @brief Returns true if the given control was pressed in the last draw().
     */
    bool pressed(const std::string& controlName) const;

    /**
     * @brief Get the rectangle of a control by name.
     *
     * The returned rectangle is DPI-scaled to match what draw() renders.
     */
    bool rectOf(const std::string& controlName, Rectangle& out) const;

    /** @brief Controls parsed from the layout. */
    const std::vector<Control>& controls() const { return controls_; }

private:
    std::string filePath_;
    std::filesystem::file_time_type lastWriteTime_{};
    std::vector<Control> controls_;
    std::unordered_map<std::string, bool> pressed_;
};

}  // namespace rgl
