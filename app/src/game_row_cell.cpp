//
//  game_row_cell.cpp
//  Equinox
//

#include "game_row_cell.hpp"

#if defined(PLATFORM_SWITCH)

#include "Settings.hpp"
#include "bold_label.hpp"
#include "cartridge_art.hpp"
#include "emu/LibraryLog.hpp"
#include "image_decode.hpp"

#include <borealis/core/cache_helper.hpp>

#include <algorithm>
#include <cmath>

using namespace brls;
using namespace brls::literals;

namespace {
constexpr float kRowHeight = GameRowCell::RowHeight;
constexpr float kCartridgeHeight = GameRowCell::CartridgeHeight;
constexpr int kHeightMs = 200;
constexpr int kEntryMs = 250;
constexpr int kFocusMs = 120;

constexpr float kCartW = 240;
constexpr float kCartH = 140;
// Room above the cartridge for its ring (6) and the float (7).
constexpr float kCartTop = 22;
constexpr float kNameTop = 178;
constexpr float kNameWidth = 320;
constexpr float kFloatAmplitude = 7;
constexpr float kFloatSeconds = 3.8f;
constexpr float kTiltDegrees = 0.5f;

constexpr float kThumbW = 48;
constexpr float kThumbH = 34;
constexpr float kThumbLeft = 8;
constexpr float kRowRadius = 10;

constexpr float kPi = 3.14159265358979f;
// The focus ring and the cartridge's float reach a little past the row.
constexpr float kCullMargin = 16;

// The cartridge shell, the one colour that is not the theme's.
const NVGcolor kCartridgeBody = nvgRGB(0xB4, 0xB4, 0xBC);

// material "file_download"
const char* const kDownloadGlyph = "\xEE\x8B\x84";

NVGcolor themeColor(const char* name) {
    return Application::getTheme().getColor(name);
}
} // namespace

GameRowCell::GameRowCell() {
    setFocusable(true);
    setHeight(kRowHeight);
    setHighlightCornerRadius(kRowRadius + 3);
    setCornerRadius(kRowRadius);

    nameLabel = new BoldLabel();
    nameLabel->setSingleLine(true);
    nameLabel->setAutoAnimate(false);
    addView(nameLabel);

    subLabel = new Label();
    subLabel->setFontSize(14);
    subLabel->setSingleLine(true);
    subLabel->setAutoAnimate(false);
    subLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    subLabel->setWidth(kNameWidth);
    subLabel->setHeight(17);
    subLabel->setMarginTop(2);
    addView(subLabel);

    yearLabel = new Label();
    yearLabel->setFontSize(14);
    yearLabel->setSingleLine(true);
    yearLabel->setMarginLeft(12);
    addView(yearLabel);

    statusLabel = new Label();
    statusLabel->setFontSize(14);
    statusLabel->setSingleLine(true);
    statusLabel->setMarginLeft(10);
    addView(statusLabel);

    downloadIcon = new IconLabel();
    downloadIcon->setFontSize(18);
    downloadIcon->setText(kDownloadGlyph);
    downloadIcon->setMarginLeft(10);
    addView(downloadIcon);

    heightAnim.setTickCallback([this]() { setHeight(heightAnim.getValue()); });
    heightAnim.setEndCallback([this](bool) {
        setHeight(targetHeight);
        setClipsToBounds(false);
    });

    applyColors();
    applyLayout();
    applyStatus();

    addGestureRecognizer(new TapGestureRecognizer(this));
    registerClickAction([this](View*) {
        if (onPlay)
            onPlay();
        return true;
    });
    updateActionHint(BUTTON_A, "emu/play"_i18n);
}

GameRowCell::~GameRowCell() {
    clearCover();
}

bool GameRowCell::reduceMotion() const {
    return Settings::instance().reduce_motion();
}

void GameRowCell::rebind(size_t index, RowLook next) {
    rowIndex = index;
    setVisibility(Visibility::VISIBLE);
    focusAlpha.reset(isFocused() ? 1.0f : 0.0f);
    if (next != look)
        setLook(next, false);
    else
        settle();
}

void GameRowCell::release() {
    rowIndex = NoGame;
    onPlay = nullptr;
    clearCover();
    setVisibility(Visibility::GONE);
}

void GameRowCell::settle() {
    heightAnim.reset(targetHeight);
    setHeight(targetHeight);
    setClipsToBounds(false);
    entry.reset(1.0f);
    floatStart = getCPUTimeUsec();
}

void GameRowCell::setGame(const std::string& name, const std::string& sub,
                          const std::string& yearText) {
    // setText ignores a text the label already has.
    nameLabel->setText(name);
    subLabel->setText(sub);
    yearLabel->setText(yearText);
}

void GameRowCell::setDataState(bool data, std::optional<int> pct, bool queue) {
    if (data == hasData && pct == percent && queue == queued)
        return;
    hasData = data;
    percent = pct;
    queued = queue;
    applyStatus();
}

void GameRowCell::setLook(RowLook next, bool animate) {
    if (next == look)
        return;
    look = next;
    applyLayout();
    applyStatus();

    targetHeight = look == RowLook::Cartridge ? kCartridgeHeight : kRowHeight;
    const float from = heightAnim.getValue();
    if (animate && !reduceMotion() && from != targetHeight) {
        heightAnim.reset(from);
        heightAnim.addStep(targetHeight, kHeightMs,
                           EasingFunction::quadraticOut);
        setClipsToBounds(true);
        heightAnim.start();
    } else {
        heightAnim.reset(targetHeight);
        setHeight(targetHeight);
        setClipsToBounds(false);
    }

    if (look != RowLook::Cartridge) {
        entry.reset(1.0f);
        return;
    }
    if (animate && !reduceMotion()) {
        entry.reset(0.0f);
        entry.addStep(1.0f, kEntryMs, EasingFunction::cubicOut);
        entry.start();
        floatStart = getCPUTimeUsec() + kEntryMs * 1000;
    } else {
        entry.reset(1.0f);
        floatStart = getCPUTimeUsec();
    }
}

void GameRowCell::applyLayout() {
    const bool cartridge = look == RowLook::Cartridge;
    const bool thumb = look == RowLook::Thumb;

    // The cartridge draws its own ring; rows use the borealis highlight.
    setHideHighlight(cartridge);
    subLabel->setVisibility(cartridge ? Visibility::VISIBLE : Visibility::GONE);
    yearLabel->setVisibility(thumb ? Visibility::VISIBLE : Visibility::GONE);

    if (cartridge) {
        setAxis(Axis::COLUMN);
        setAlignItems(AlignItems::CENTER);
        setJustifyContent(JustifyContent::FLEX_START);
        setPadding(kNameTop, 0, 0, 0);
        nameLabel->setFontSize(16);
        nameLabel->setGrow(0);
        nameLabel->setShrink(0);
        nameLabel->setWidth(kNameWidth);
        nameLabel->setHeight(20);
        nameLabel->setMarginLeft(0);
        nameLabel->setHorizontalAlign(HorizontalAlign::CENTER);
        return;
    }
    setAxis(Axis::ROW);
    setAlignItems(AlignItems::CENTER);
    setJustifyContent(JustifyContent::FLEX_START);
    if (thumb)
        setPadding(4, 12, 4, kThumbLeft);
    else
        setPadding(0, 14, 0, 14);
    nameLabel->setFontSize(15);
    nameLabel->setGrow(1);
    nameLabel->setShrink(1);
    nameLabel->setWidth(View::AUTO);
    nameLabel->setHeight(View::AUTO);
    nameLabel->setMarginLeft(thumb ? kThumbW + 12 : 0);
    nameLabel->setHorizontalAlign(HorizontalAlign::LEFT);
}

void GameRowCell::applyStatus() {
    const bool inRow = look != RowLook::Cartridge;
    std::string text;
    if (percent)
        text = std::to_string(*percent) + "%";
    else if (queued)
        text = "emu/queued"_i18n;
    statusLabel->setText(text);
    statusLabel->setVisibility(inRow && !text.empty() ? Visibility::VISIBLE
                                                      : Visibility::GONE);
    downloadIcon->setVisibility(inRow && !hasData && text.empty()
                                    ? Visibility::VISIBLE
                                    : Visibility::GONE);
}

void GameRowCell::applyColors() {
    const NVGcolor muted = themeColor("equinox/muted");
    subLabel->setTextColor(muted);
    yearLabel->setTextColor(muted);
    statusLabel->setTextColor(muted);
    downloadIcon->setTextColor(muted);
    ringColor = themeColor("brls/highlight/color1");
    groundColor = themeColor("brls/background");
    emptyThumbColor = themeColor("equinox/card_inner");
}

void GameRowCell::willAppear(bool resetState) {
    Box::willAppear(resetState);
    applyColors();
}

void GameRowCell::onFocusGained() {
    Box::onFocusGained();
    if (reduceMotion()) {
        focusAlpha.reset(1.0f);
    } else {
        focusAlpha.reset();
        focusAlpha.addStep(1.0f, kFocusMs, EasingFunction::quadraticOut);
        focusAlpha.start();
    }
    if (onFocused)
        onFocused();
}

void GameRowCell::onFocusLost() {
    Box::onFocusLost();
    if (reduceMotion()) {
        focusAlpha.reset(0.0f);
    } else {
        focusAlpha.reset();
        focusAlpha.addStep(0.0f, kFocusMs, EasingFunction::quadraticOut);
        focusAlpha.start();
    }
}

void GameRowCell::setCover(const std::string& path) {
    if (path == coverWanted)
        return;
    clearCover();
    coverWanted = path;
    emu::liblog::covers.asked++;
    if (useCachedCover(path)) {
        emu::liblog::covers.cacheHits++;
        return;
    }

    const unsigned wanted = coverTicket->load();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, path, wanted, ticket = coverTicket]() {
        const int64_t started = emu::liblog::nowUs();
        DecodedImage image = decodeIfWanted(ticket, wanted, path, kCoverMinW, kCoverMinH);
        if (image.pixels) {
            const int64_t took = emu::liblog::nowUs() - started;
            emu::liblog::covers.decoded++;
            emu::liblog::covers.decodeTotalUs += took;
            emu::liblog::raiseMax(emu::liblog::covers.decodeMaxUs, took);
        }
        brls::sync([ASYNC_TOKEN, path, image]() {
            ASYNC_RELEASE
            applyCover(path, image.pixels, image.width, image.height);
        });
    });
}

void GameRowCell::applyCover(const std::string& path,
                             std::shared_ptr<unsigned char> pixels, int width,
                             int height) {
    if (path != coverWanted) {
        emu::liblog::covers.dropped++;
        return;
    }
    if (useCachedCover(path))
        return;
    if (!pixels) {
        emu::liblog::bug("cover did not decode: {}", path);
        return;
    }

    const int64_t started = emu::liblog::nowUs();
    const int tex = nvgCreateImageRGBA(Application::getNVGContext(), width,
                                       height, 0, pixels.get());
    emu::liblog::raiseMax(emu::liblog::covers.uploadMaxUs,
                          emu::liblog::nowUs() - started);
    if (tex <= 0) {
        emu::liblog::bug("no texture for cover ({}x{}): {}", width, height, path);
        return;
    }
    setCoverTexture(tex);
    TextureCache::instance().addCache(path, tex);
}

bool GameRowCell::useCachedCover(const std::string& path) {
    const int tex = TextureCache::instance().getCache(path);
    if (tex <= 0)
        return false;
    setCoverTexture(tex);
    return true;
}

// Same bookkeeping as RomCell: the cache owns the textures.
void GameRowCell::setCoverTexture(int tex) {
    if (coverTex > 0)
        TextureCache::instance().removeCache(coverTex);
    coverTex = tex;
}

void GameRowCell::clearCover() {
    coverWanted.clear();
    coverTicket->fetch_add(1);
    setCoverTexture(0);
}

void GameRowCell::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    // Box::draw skips only leaf views out of sight, and a row is a Box: the
    // window keeps rows above and below the screen, drawn for nothing.
    for (Box* bounds = getParent(); bounds; bounds = bounds->getParent()) {
        float top, right, bottom, left;
        bounds->getCullingBounds(&top, &right, &bottom, &left);
        if (y + height + kCullMargin < top || y - kCullMargin > bottom)
            return;
    }
    if (look == RowLook::Cartridge)
        drawCartridgeLook(vg, x, y, width);
    else if (look == RowLook::Thumb)
        drawThumb(vg, x, y, height);
    Box::draw(vg, x, y, width, height, style, ctx);
}

void GameRowCell::drawCartridgeLook(NVGcontext* vg, float x, float y,
                                    float width) {
    const bool still = reduceMotion();
    const float appear = still ? 1.0f : entry.getValue();
    const float seconds =
        still ? 0.0f
              : std::max<float>(0, (getCPUTimeUsec() - floatStart) / 1e6f);
    const float phase = std::sin(2 * kPi * seconds / kFloatSeconds);
    // 0 on the ground, 1 at the top of the float.
    const float lift = (1 + phase) / 2;

    const float cx = x + width / 2;
    const float top = y + kCartTop;
    const float groundY = top + kCartH + 8;
    const float groundW = kCartW * 0.72f;
    const float groundScale = still ? 1.0f : 1 - 0.18f * lift;
    const float groundAlpha = still ? 0.55f : 0.55f - 0.25f * lift;

    nvgSave(vg);
    nvgGlobalAlpha(vg, appear);

    nvgSave(vg);
    nvgTranslate(vg, cx, groundY);
    nvgScale(vg, groundScale, 1);
    nvgBeginPath(vg);
    nvgRect(vg, -groundW / 2 - 12, -18, groundW + 24, 36);
    nvgFillPaint(vg, nvgBoxGradient(vg, -groundW / 2, -6, groundW, 12, 6, 8,
                                    nvgRGBAf(0, 0, 0, groundAlpha),
                                    nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    nvgRestore(vg);

    const float rise = (1 - appear) * 8;
    const float bob = still ? 0.0f : -kFloatAmplitude * phase;
    const float tilt = still ? 0.0f : kTiltDegrees * kPi / 180 * phase;
    nvgTranslate(vg, cx, top + kCartH / 2 + rise + bob);
    nvgRotate(vg, tilt);

    int texW = 0, texH = 0;
    const int tex = coverTex;
    if (tex > 0)
        nvgImageSize(vg, tex, &texW, &texH);
    drawCartridge(vg, -kCartW / 2, -kCartH / 2, kCartW, kCartH, tex, texW,
                  texH, kCartridgeBody, ringColor, groundColor, focusAlpha.getValue());
    nvgRestore(vg);
}

void GameRowCell::drawThumb(NVGcontext* vg, float x, float y, float height) {
    const float tx = x + kThumbLeft;
    const float ty = y + (height - kThumbH) / 2;
    const int tex = coverTex;
    int texW = 0, texH = 0;
    if (tex > 0)
        nvgImageSize(vg, tex, &texW, &texH);
    if (texW <= 0 || texH <= 0) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tx, ty, kThumbW, kThumbH, 3);
        nvgFillColor(vg, emptyThumbColor);
        nvgFill(vg);
        return;
    }
    const float fit = std::min(kThumbW / texW, kThumbH / texH);
    const float w = texW * fit;
    const float h = texH * fit;
    const float ix = tx + (kThumbW - w) / 2;
    const float iy = ty + (kThumbH - h) / 2;
    nvgBeginPath(vg);
    nvgRect(vg, ix, iy, w, h);
    nvgFillPaint(vg, nvgImagePattern(vg, ix, iy, w, h, 0, tex, 1));
    nvgFill(vg);
}

#endif // PLATFORM_SWITCH
