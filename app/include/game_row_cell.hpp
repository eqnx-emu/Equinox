//
//  game_row_cell.hpp
//  Equinox
//
//  One game in the library list: a floating cartridge for the picked game,
//  or a row with a thumbnail, or a row with just the name.
//

#pragma once

#include <borealis.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>

enum class RowLook { Cartridge, Thumb, Name };

class GameRowCell : public brls::Box {
  public:
    static constexpr size_t NoGame = static_cast<size_t>(-1);
    static constexpr float RowHeight = 48;
    static constexpr float CartridgeHeight = 216;

    GameRowCell();
    ~GameRowCell() override;

    // The game this row shows, or NoGame while it waits in the pool.
    size_t index() const { return rowIndex; }
    // Starts showing another game, at rest: no animation carries over.
    void rebind(size_t index, RowLook look);
    // Back to the pool: hidden, and its cover released.
    void release();
    // The height right now, while it eases too.
    float currentHeight() { return heightAnim.getValue(); }

    // sub: the line under the cartridge's name ("year · genre" or "No data").
    void setGame(const std::string& name, const std::string& sub,
                 const std::string& yearLabel);
    // Decoded off the UI thread; a row that moves on abandons the decoding.
    void setCover(const std::string& path);
    void clearCover();
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
    size_t rowIndex = NoGame;
    RowLook look = RowLook::Name;
    bool reduceMotion() const;

    int coverTex = 0; // a reference held in the TextureCache
    std::string coverWanted;
    // Bumped when the row stops wanting the cover being decoded.
    std::shared_ptr<std::atomic<unsigned>> coverTicket =
        std::make_shared<std::atomic<unsigned>>(0);
    std::string shownName, shownSub, shownYear;

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
    void settle();
    void drawCartridgeLook(NVGcontext* vg, float x, float y, float width);
    void drawThumb(NVGcontext* vg, float x, float y, float height);
};
