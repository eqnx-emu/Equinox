//
//  game_row_cell.hpp
//  Equinox
//
//  One game in the library list: a floating cartridge for the picked game,
//  or a row with a thumbnail, or a row with just the name.
//

#pragma once

#include "emu/Console.hpp"
#include <borealis.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

enum class RowLook { Cartridge, Thumb, Name };

class GameRowCell : public brls::Box {
  public:
    GameRowCell(const emu::Console& console, size_t index);

    size_t index() const { return rowIndex; }

    // sub: the line under the cartridge's name ("year · genre" or "No data").
    void setGame(const std::string& name, const std::string& sub,
                 const std::string& yearLabel);
    // Decoded off the UI thread.
    void setCover(const std::string& path);
    // The height eases between 48 and 216 px, unless animate is false or
    // "Reduce motion" is on. Only the cartridge animates.
    void setLook(RowLook look, bool animate);
    // Rows show a download icon without data, "37%" while downloading and
    // "queued" while waiting; percent wins over queued.
    void setDataState(bool hasData, std::optional<int> percent, bool queued);

    std::function<void()> onFocused;
    std::function<void()> onPlay;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void willAppear(bool resetState = false) override;
    void onFocusGained() override;
    void onFocusLost() override;

  private:
    size_t rowIndex;
    RowLook look = RowLook::Name;
    bool reduceMotion() const;

    brls::Image* texture = nullptr;
    brls::Label* nameLabel = nullptr;
    brls::Label* subLabel = nullptr;
    brls::Label* yearLabel = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Label* downloadIcon = nullptr;

    bool hasData = true;
    std::optional<int> percent;
    bool queued = false;
    float targetHeight = 48;

    brls::Animatable heightAnim = 48.0f;
    brls::Animatable entry = 1.0f;
    brls::Animatable focusAlpha = 0.0f;
    brls::Time floatStart = 0;

    void applyLayout();
    void applyStatus();
    void applyColors();
    void applyCover(const std::string& path,
                    std::shared_ptr<unsigned char> pixels, int width,
                    int height);
    bool useCachedCover(const std::string& path);
    void setCoverTexture(int texture);
    void drawCartridgeLook(NVGcontext* vg, float x, float y, float width);
    void drawThumb(NVGcontext* vg, float x, float y, float height);
};
