//
//  game_details_panel.cpp
//  Equinox
//

#include "game_details_panel.hpp"

#if defined(PLATFORM_SWITCH)

#include "Settings.hpp"
#include "bold_label.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/SsCredentials.hpp"
#include "library_widgets.hpp"

#include <borealis/extern/nanovg/stb_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

using namespace brls;

namespace {
constexpr float kSnapW = 292, kSnapH = 256, kSnapRadius = 10;
constexpr float kTitleW = 148, kTitleH = 112, kTitleRadius = 8;
constexpr float kButtonHeight = 44;
constexpr float kBarWidth = 400;
constexpr int kFadeMs = 150;
constexpr float kPi = 3.14159265358979f;

NVGcolor themeColor(const char* name) {
    return Application::getTheme().getColor(name);
}

std::string text(const std::string& key) {
    return getStr(key);
}

int64_t fileStamp(const std::string& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(emu::fromUtf8(path), ec);
    return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
}

std::shared_ptr<unsigned char> decodeImage(const std::string& path, int& width, int& height) {
    int channels = 0;
    unsigned char* px = stbi_load(path.c_str(), &width, &height, &channels, 4);
    return std::shared_ptr<unsigned char>(px, [](unsigned char* p) {
        if (p)
            stbi_image_free(p);
    });
}

void dashedFrame(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor color) {
    constexpr float dash = 5, gap = 4;
    const auto edge = [&](float x0, float y0, float x1, float y1) {
        const float len = std::hypot(x1 - x0, y1 - y0);
        const float dx = (x1 - x0) / len, dy = (y1 - y0) / len;
        for (float t = 0; t < len; t += dash + gap) {
            const float end = std::min(t + dash, len);
            nvgMoveTo(vg, x0 + dx * t, y0 + dy * t);
            nvgLineTo(vg, x0 + dx * end, y0 + dy * end);
        }
    };
    const auto corner = [&](float cx, float cy, float from) {
        nvgMoveTo(vg, cx + r * std::cos(from), cy + r * std::sin(from));
        nvgArc(vg, cx, cy, r, from, from + kPi / 2, NVG_CW);
    };
    nvgBeginPath(vg);
    edge(x + r, y, x + w - r, y);
    edge(x + w, y + r, x + w, y + h - r);
    edge(x + w - r, y + h, x + r, y + h);
    edge(x, y + h - r, x, y + r);
    corner(x + r, y + r, kPi);
    corner(x + w - r, y + r, 1.5f * kPi);
    corner(x + w - r, y + h - r, 0);
    corner(x + r, y + h - r, kPi / 2);
    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);
}

// An image cropped to fill its frame, or a dashed frame with a short text.
// The file is decoded off the UI thread.
class ArtFrame : public Box {
  public:
    ArtFrame(float w, float h, float r, float fontSize) : radius(r) {
        setWidth(w);
        setHeight(h);
        setShrink(0);
        setAlignItems(AlignItems::CENTER);
        setJustifyContent(JustifyContent::CENTER);
        label = new Label();
        label->setFontSize(fontSize);
        label->setWidth(w - 16);
        label->setHorizontalAlign(HorizontalAlign::CENTER);
        label->setTextColor(themeColor("equinox/muted"));
        addView(label);
    }

    ~ArtFrame() override { freeTexture(); }

    // An empty path shows the dashed frame, with placeholder in it.
    void set(const std::string& imagePath, const std::string& placeholder) {
        label->setText(placeholder);
        label->setVisibility(imagePath.empty() && !placeholder.empty() ? Visibility::VISIBLE
                                                                       : Visibility::GONE);
        label->setTextColor(themeColor("equinox/muted"));
        const int64_t stamp = imagePath.empty() ? 0 : fileStamp(imagePath);
        if (imagePath == path && stamp == loadedStamp)
            return;
        path = imagePath;
        loadedStamp = stamp;
        generation++;
        freeTexture();
        if (path.empty())
            return;

        const unsigned wanted = generation;
        const std::string file = path;
        ASYNC_RETAIN
        async([ASYNC_TOKEN, wanted, file]() {
            int width = 0, height = 0;
            auto pixels = decodeImage(file, width, height);
            sync([ASYNC_TOKEN, wanted, pixels, width, height]() {
                ASYNC_RELEASE
                if (wanted != generation || !pixels)
                    return;
                tex = nvgCreateImageRGBA(Application::getNVGContext(), width, height,
                                         NVG_IMAGE_NEAREST, pixels.get());
                texW = width;
                texH = height;
            });
        });
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, Style style,
              FrameContext* ctx) override {
        if (tex > 0 && texW > 0 && texH > 0) {
            const float fit = std::max(width / texW, height / texH);
            const float iw = texW * fit, ih = texH * fit;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillPaint(vg, a(nvgImagePattern(vg, x + (width - iw) / 2, y + (height - ih) / 2, iw,
                                               ih, 0, tex, 1)));
            nvgFill(vg);
        } else if (!path.empty()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillColor(vg, a(themeColor("equinox/card_inner")));
            nvgFill(vg);
        } else {
            NVGcolor dash = themeColor("equinox/muted");
            dash.a *= 0.3f;
            dashedFrame(vg, x, y, width, height, radius, a(dash));
        }
        Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    float radius;
    Label* label = nullptr;
    std::string path;
    int64_t loadedStamp = 0;
    unsigned generation = 0;
    int tex = 0, texW = 0, texH = 0;

    void freeTexture() {
        if (tex > 0)
            nvgDeleteImage(Application::getNVGContext(), tex);
        tex = texW = texH = 0;
    }
};

Label* makeLabel(float size) {
    auto* label = new Label();
    label->setFontSize(size);
    return label;
}

Button* makeButton(const ButtonStyle* style, const std::string& caption,
                   const std::function<void()>& onClick) {
    auto* button = new Button();
    button->setStyle(style);
    button->setText(caption);
    button->setFontSize(15);
    button->setHeight(kButtonHeight);
    button->setShrink(0);
    button->registerClickAction([onClick](View*) {
        onClick();
        return true;
    });
    return button;
}

std::string stepText(emu::DataStep step) {
    switch (step) {
    case emu::DataStep::Identify:
        return text("library/details/step_identify");
    case emu::DataStep::Snap:
        return text("library/details/step_snap");
    case emu::DataStep::Title:
        return text("library/details/step_title");
    case emu::DataStep::Synopsis:
        return text("library/details/step_synopsis");
    case emu::DataStep::Done:
        break;
    }
    return text("library/details/step_done");
}

} // namespace

GameDetailsPanel::GameDetailsPanel() : Box(Axis::COLUMN) {
    setPadding(28, 36, 12, 36);
    setGrow(1);

    content = new Box(Axis::COLUMN);
    content->setGrow(1);
    addView(content);

    auto download = [this]() {
        if (onDownload)
            onDownload();
    };

    title = new BoldLabel();
    title->setFontSize(30);
    title->setShrink(0);
    content->addView(title);

    fileLine = makeLabel(15);
    fileLine->setMarginTop(8);
    fileLine->setShrink(0);
    content->addView(fileLine);

    auto* action = new Box(Axis::COLUMN);
    action->setAlignItems(AlignItems::FLEX_START);
    action->setMarginTop(12);
    action->setShrink(0);
    content->addView(action);

    downloadButton =
        makeButton(&BUTTONSTYLE_PRIMARY, text("library/details/download"), download);
    action->addView(downloadButton);

    queuedRow = new Box(Axis::ROW);
    queuedRow->setAlignItems(AlignItems::CENTER);
    waitingLabel = makeLabel(15);
    waitingLabel->setText(text("library/details/waiting"));
    queuedRow->addView(waitingLabel);
    queuedLabel = makeLabel(15);
    queuedLabel->setText(text("library/details/queued"));
    queuedLabel->setMarginLeft(12);
    queuedRow->addView(queuedLabel);
    action->addView(queuedRow);

    progressBox = new Box(Axis::COLUMN);
    progressBox->setWidth(kBarWidth);
    auto* progressLine = new Box(Axis::ROW);
    progressLine->setJustifyContent(JustifyContent::SPACE_BETWEEN);
    stepLabel = makeLabel(15);
    progressLine->addView(stepLabel);
    percentLabel = new BoldLabel();
    percentLabel->setFontSize(15);
    progressLine->addView(percentLabel);
    progressBox->addView(progressLine);
    auto* progress = new LibraryProgressBar();
    progress->setWidth(kBarWidth);
    bar = progress;
    bar->setMarginTop(6);
    progressBox->addView(bar);
    action->addView(progressBox);

    failedRow = new Box(Axis::ROW);
    failedRow->setAlignItems(AlignItems::CENTER);
    failedLabel = makeLabel(15);
    failedLabel->setShrink(1);
    failedLabel->setMarginRight(12);
    failedRow->addView(failedLabel);
    retryButton = makeButton(&BUTTONSTYLE_PRIMARY, text("library/details/retry"), download);
    failedRow->addView(retryButton);
    action->addView(failedRow);

    readyRow = new Box(Axis::ROW);
    readyRow->setAlignItems(AlignItems::CENTER);
    readyLabel = makeLabel(15);
    readyLabel->setText(text("library/details/ready"));
    readyLabel->setMarginRight(12);
    readyRow->addView(readyLabel);
    updateButton = makeButton(&BUTTONSTYLE_DEFAULT, text("library/details/update"), download);
    readyRow->addView(updateButton);
    action->addView(readyRow);

    auto* media = new Box(Axis::ROW);
    media->setAlignItems(AlignItems::FLEX_START);
    media->setMarginTop(20);
    media->setShrink(0);
    content->addView(media);

    snapFrame = new ArtFrame(kSnapW, kSnapH, kSnapRadius, 15);
    media->addView(snapFrame);

    auto* titleColumn = new Box(Axis::COLUMN);
    titleColumn->setMarginLeft(20);
    titleColumn->setShrink(0);
    titleFrame = new ArtFrame(kTitleW, kTitleH, kTitleRadius, 14);
    titleColumn->addView(titleFrame);
    titleCaption = makeLabel(14);
    titleCaption->setText(text("library/details/title_caption"));
    titleCaption->setMarginTop(8);
    titleColumn->addView(titleCaption);
    media->addView(titleColumn);

    facts = new Box(Axis::COLUMN);
    facts->setMarginLeft(20);
    facts->setGrow(1);
    facts->setShrink(1);
    facts->setWidth(0);
    media->addView(facts);

    // Clips a very long synopsis at the bottom so "Fontes" stays in place.
    auto* synopsis = new Box(Axis::COLUMN);
    synopsis->setMarginTop(20);
    synopsis->setMaxWidth(760);
    synopsis->setGrow(1);
    synopsis->setShrink(1);
    synopsis->setClipsToBounds(true);
    synopsisHeading = new BoldLabel();
    synopsisHeading->setFontSize(16);
    synopsisHeading->setText(text("library/details/synopsis"));
    synopsis->addView(synopsisHeading);
    synopsisText = makeLabel(16);
    synopsisText->setMarginTop(8);
    synopsis->addView(synopsisText);
    content->addView(synopsis);

    sources = makeLabel(14);
    sources->setMarginTop(12);
    sources->setShrink(0);
    content->addView(sources);

    fade.setTickCallback([this]() { content->setAlpha(fade.getValue()); });
    fade.setEndCallback([this](bool) { content->setAlpha(1.0f); });

    applyColors();
    applyDetails();
}

void GameDetailsPanel::willAppear(bool resetState) {
    Box::willAppear(resetState);
    applyColors();
}

void GameDetailsPanel::applyColors() {
    const NVGcolor muted = themeColor("equinox/muted");
    fileLine->setTextColor(muted);
    queuedLabel->setTextColor(muted);
    readyLabel->setTextColor(muted);
    titleCaption->setTextColor(muted);
    sources->setTextColor(muted);
    updateButton->setBackgroundColor(themeColor("equinox/card_inner"));
    // The account can change in Settings while the panel is off screen.
    sources->setText(text(emu::ssConfigured(emu::appSsConfig()) ? "library/details/sources"
                                                                : "library/details/sources_free"));
}

void GameDetailsPanel::show(const GameDetails& next, bool animate) {
    details = next;
    if (animate) {
        step.reset();
        queued = false;
        failed.reset();
    }
    applyDetails();
    startFade(animate);
}

void GameDetailsPanel::setProgress(std::optional<emu::DataStep> nextStep, bool nextQueued) {
    step = nextStep;
    queued = nextQueued;
    if (step || queued)
        failed.reset();
    applyAction();
}

void GameDetailsPanel::setFailed(std::optional<emu::DataStop> stop) {
    if (stop && (*stop == emu::DataStop::None || *stop == emu::DataStop::Cancelled))
        stop.reset();
    failed = stop;
    applyAction();
}

void GameDetailsPanel::startFade(bool animate) {
    if (!animate || Settings::instance().reduce_motion()) {
        fade.reset(1.0f);
        content->setAlpha(1.0f);
        return;
    }
    fade.reset(0.0f);
    content->setAlpha(0.0f);
    fade.addStep(1.0f, kFadeMs, EasingFunction::linear);
    fade.start();
}

void GameDetailsPanel::applyDetails() {
    title->setText(details.name);
    fileLine->setText(brls::getStr("library/details/not_identified", details.fileName));
    fileLine->setVisibility(details.identified ? Visibility::GONE : Visibility::VISIBLE);
    synopsisText->setText(details.synopsis.empty() ? text("library/details/synopsis_none")
                                                   : details.synopsis);
    applyAction();
    applyMedia();
    applyFacts();
}

void GameDetailsPanel::applyAction() {
    Action state = Action::Missing;
    if (queued && !step)
        state = Action::Queued;
    else if (step)
        state = Action::Downloading;
    else if (failed)
        state = Action::Failed;
    else if (details.hasData)
        state = Action::Ready;

    const auto shown = [&](Action a) { return state == a ? Visibility::VISIBLE : Visibility::GONE; };
    downloadButton->setVisibility(shown(Action::Missing));
    queuedRow->setVisibility(shown(Action::Queued));
    progressBox->setVisibility(shown(Action::Downloading));
    failedRow->setVisibility(shown(Action::Failed));
    readyRow->setVisibility(shown(Action::Ready));

    if (step) {
        stepLabel->setText(stepText(*step));
        const float fraction = emu::stepProgress(*step);
        percentLabel->setText(std::to_string(static_cast<int>(std::lround(fraction * 100))) + "%");
        static_cast<LibraryProgressBar*>(bar)->setFraction(fraction);
    }
    if (failed)
        failedLabel->setText(brls::getStr("library/details/failed", libraryStopText(*failed)));
}

void GameDetailsPanel::applyMedia() {
    const bool data = details.hasData;
    std::string snapNote = text("library/details/snap_placeholder");
    std::string titleNote;
    if (data && details.snapPath.empty() && details.snap == emu::PartState::Missing)
        snapNote = text("library/details/snap_missing");
    if (data && details.titlePath.empty() && details.title == emu::PartState::Missing)
        titleNote = text("library/details/title_missing");

    static_cast<ArtFrame*>(snapFrame)->set(data ? details.snapPath : "", snapNote);
    static_cast<ArtFrame*>(titleFrame)->set(data ? details.titlePath : "", titleNote);
}

void GameDetailsPanel::applyFacts() {
    facts->clearViews();

    struct Fact {
        const char* label;
        std::string value;
    };
    std::vector<Fact> list;
    if (details.hasData) {
        if (const emu::GameInfo* info = details.info) {
            if (!info->genre.empty())
                list.push_back({"library/fact/genre", info->genre});
            if (info->players > 0)
                list.push_back({"library/fact/players", std::to_string(info->players)});
            if (!info->developer.empty())
                list.push_back({"library/fact/developer", info->developer});
            if (!info->publisher.empty())
                list.push_back({"library/fact/publisher", info->publisher});
            if (info->year > 0)
                list.push_back({"library/fact/released", std::to_string(info->year)});
        }
        if (!details.regions.empty()) {
            std::string joined;
            for (const std::string& region : details.regions) {
                if (!joined.empty())
                    joined += ", ";
                joined += text("library/region/" + region);
            }
            list.push_back({"library/fact/region", joined});
        }
        if (details.info && !details.info->franchise.empty())
            list.push_back({"library/fact/franchise", details.info->franchise});
    }

    if (list.empty()) {
        auto* sentence = makeLabel(15);
        sentence->setText(text(details.hasData ? "library/details/facts_none"
                                               : "library/details/facts_pending"));
        sentence->setTextColor(themeColor("equinox/muted"));
        facts->addView(sentence);
        return;
    }

    for (size_t i = 0; i < list.size(); i += 2) {
        auto* row = new Box(Axis::ROW);
        row->setMarginBottom(14);
        for (size_t j = i; j < std::min(i + 2, list.size()); j++) {
            auto* cell = new Box(Axis::COLUMN);
            cell->setWidthPercentage(50);
            cell->setPaddingRight(20);
            auto* label = makeLabel(14);
            label->setText(text(list[j].label));
            label->setTextColor(themeColor("equinox/muted"));
            cell->addView(label);
            auto* value = makeLabel(16);
            value->setText(list[j].value);
            value->setMarginTop(3);
            cell->addView(value);
            row->addView(cell);
        }
        facts->addView(row);
    }
}

#endif // PLATFORM_SWITCH
