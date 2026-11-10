//
//  rom_cell.cpp
//  Equinox
//

#include "rom_cell.hpp"

#if defined(EQX_EMU_UI)

#include "bold_label.hpp"
#include "image_decode.hpp"

#include <borealis/core/cache_helper.hpp>

using namespace brls;
using namespace brls::literals;

namespace {
constexpr float kWidth = 180;
constexpr float kHeight = 216;
constexpr float kRadius = 12;

NVGcolor hex(unsigned rgb) {
    return nvgRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}
} // namespace

RomCell::RomCell(const emu::Console& console, const emu::RomEntry& rom)
    : consoleRef(console), entry(rom) {
    setAxis(Axis::COLUMN);
    setWidth(kWidth);
    setHeight(kHeight);
    setFocusable(true);
    setCornerRadius(kRadius);
    setHighlightCornerRadius(kRadius + 3);

    // The generic cover: a controller over the console's gradient, with the
    // game's name, until (or unless) libretro has a cover.
    generic = new Box(Axis::COLUMN);
    generic->setDetachedPosition(0, 0);
    generic->setSize(Size(kWidth, kHeight));
    generic->setJustifyContent(JustifyContent::CENTER);
    generic->setAlignItems(AlignItems::CENTER);
    generic->setPadding(16, 14, 16, 14);

    auto* icon = new IconLabel();
    icon->setText(""); // videogame_asset
    icon->setFontSize(54);
    icon->setTextColor(Application::getTheme().getColor("equinox/sun"));
    generic->addView(icon);

    auto* name = new BoldLabel();
    name->setFontSize(15);
    name->setMarginTop(12);
    name->setHorizontalAlign(HorizontalAlign::CENTER);
    name->setTextColor(nvgRGB(238, 240, 250));
    name->setText(rom.title);
    generic->addView(name);
    addView(generic);

    image = new Image();
    image->setDetachedPosition(0, 0);
    image->setSize(Size(kWidth, kHeight));
    image->setScalingType(ImageScalingType::FIT);
    image->setCornerRadius(kRadius);
    image->setVisibility(Visibility::GONE);
    addView(image);

    // Along the bottom of the generic cover while its cover downloads; the
    // spinner is the loading dialog's, smaller.
    fetchingLayer = new Box(Axis::COLUMN);
    fetchingLayer->setDetachedPosition(0, kHeight - 64);
    fetchingLayer->setSize(Size(kWidth, 56));
    fetchingLayer->setJustifyContent(JustifyContent::CENTER);
    fetchingLayer->setAlignItems(AlignItems::CENTER);
    auto* spinner = new ProgressSpinner(ProgressSpinnerSize::NORMAL);
    spinner->View::setSize(Size(28, 28));
    fetchingLayer->addView(spinner);
    auto* fetchingText = new Label();
    fetchingText->setFontSize(13);
    fetchingText->setMarginTop(4);
    fetchingText->setHorizontalAlign(HorizontalAlign::CENTER);
    fetchingText->setTextColor(nvgRGBA(238, 240, 250, 200));
    fetchingText->setText("emu/fetching_cover"_i18n);
    fetchingLayer->addView(fetchingText);
    fetchingLayer->setVisibility(Visibility::GONE);
    addView(fetchingLayer);

    // Over the cover, under the star: same look as AppCell's offline layer.
    missingLayer = new Box(Axis::COLUMN);
    missingLayer->setDetachedPosition(0, 0);
    missingLayer->setSize(Size(kWidth, kHeight));
    missingLayer->setJustifyContent(JustifyContent::CENTER);
    missingLayer->setAlignItems(AlignItems::CENTER);
    missingLayer->setPadding(16, 14, 16, 14);
    missingLayer->setCornerRadius(kRadius);
    missingLayer->setBackgroundColor(nvgRGBA(11, 14, 31, 190));
    auto* missingIcon = new IconLabel();
    missingIcon->setFontSize(40);
    missingIcon->setText(""); // error
    missingIcon->setTextColor(nvgRGB(157, 184, 255));
    missingLayer->addView(missingIcon);
    auto* missingText = new BoldLabel();
    missingText->setFontSize(16);
    missingText->setMarginTop(8);
    missingText->setHorizontalAlign(HorizontalAlign::CENTER);
    missingText->setTextColor(nvgRGB(238, 240, 250));
    missingText->setText("favorites/rom_gone"_i18n);
    missingLayer->addView(missingText);
    missingLayer->setVisibility(Visibility::GONE);
    addView(missingLayer);

    star = new Image();
    star->setDetachedPosition(kWidth - 36, 8);
    star->setSize(Size(28, 28));
    star->setAlpha(0.75f);
    star->setScalingType(ImageScalingType::STRETCH);
    star->setImageFromRes("img/star.png");
    star->setVisibility(Visibility::GONE);
    addView(star);

    addGestureRecognizer(new TapGestureRecognizer(this));
    registerClickAction([this](View*) {
        if (beforeLaunch && !beforeLaunch())
            return true;
        if (onPlay)
            onPlay();
        return true;
    });
    updateActionHint(BUTTON_A, "emu/play"_i18n);
}

void RomCell::setFavorite(bool favorite) {
    star->setVisibility(favorite ? Visibility::VISIBLE : Visibility::GONE);
}

void RomCell::setMissing(bool missing) {
    missingLayer->setVisibility(missing ? Visibility::VISIBLE
                                        : Visibility::GONE);
}

void RomCell::setFetching(bool fetching) {
    const bool show = fetching && !coverShown;
    fetchingLayer->setVisibility(show ? Visibility::VISIBLE : Visibility::GONE);
    // The controller and the name move up, clear of the spinner.
    generic->setPaddingBottom(show ? 64 : 16);
}

void RomCell::loadCover(const std::string& path) {
    if (useCachedCover(path))
        return;

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, path]() {
        DecodedImage image = decodeImageFile(path, kCoverMinW, kCoverMinH);
        brls::sync([ASYNC_TOKEN, path, image]() {
            ASYNC_RELEASE
            applyCover(path, image.pixels, image.width, image.height);
        });
    });
}

void RomCell::applyCover(const std::string& path,
                         std::shared_ptr<unsigned char> pixels, int width,
                         int height) {
    if (useCachedCover(path) || !pixels)
        return;

    const int texture = nvgCreateImageRGBA(Application::getNVGContext(), width,
                                           height, 0, pixels.get());
    if (texture <= 0)
        return;
    setCoverTexture(texture);
    TextureCache::instance().addCache(path, texture);
}

bool RomCell::useCachedCover(const std::string& path) {
    const int texture = TextureCache::instance().getCache(path);
    if (texture <= 0)
        return false;
    setCoverTexture(texture);
    return true;
}

// Same bookkeeping as AppCell: the cache owns the textures.
void RomCell::setCoverTexture(int texture) {
    if (image->getTexture() > 0)
        TextureCache::instance().removeCache(image->getTexture());
    image->setFreeTexture(false);
    image->innerSetImage(texture);
    image->setVisibility(Visibility::VISIBLE);
    generic->setVisibility(Visibility::GONE);
    fetchingLayer->setVisibility(Visibility::GONE);
    coverShown = true;
}

void RomCell::draw(NVGcontext* vg, float x, float y, float width, float height,
                   Style style, FrameContext* ctx) {
    // The console's colours behind the generic cover, and around a cover
    // that does not fill the frame.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, kRadius);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + height,
                                       a(hex(consoleRef.cardColorTop)),
                                       a(hex(consoleRef.cardColorBottom))));
    nvgFill(vg);

    Box::draw(vg, x, y, width, height, style, ctx);
}

#endif // EQX_EMU_UI
