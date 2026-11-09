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
#include "image_decode.hpp"
#include "library_widgets.hpp"

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
constexpr float kStickDeadzone = 0.2f;
constexpr float kStickPixelsPerFrame = 8;
constexpr float kSynopsisIndicatorRoom = 20;
// Cells made up front; applyFacts adds rows if a game ever lists more.
constexpr size_t kUsualFacts = 8;

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
    ArtFrame(float w, float h, float r, float fontSize) : radius(r), frameW(w), frameH(h) {
        setWidth(w);
        setHeight(h);
        setShrink(0);
        setAlignItems(AlignItems::CENTER);
        setJustifyContent(JustifyContent::CENTER);
        label = new Label();
        label->setFontSize(fontSize);
        label->setWidth(w - 16);
        label->setHorizontalAlign(HorizontalAlign::CENTER);
        addView(label);
        applyColors();
    }

    // Kept, not looked up by name while drawing.
    void applyColors() {
        muted = themeColor("equinox/muted");
        empty = themeColor("equinox/card_inner");
        label->setTextColor(muted);
    }

    ~ArtFrame() override { freeTexture(); }

    // An empty path shows the dashed frame, with placeholder in it.
    void set(const std::string& imagePath, const std::string& placeholder) {
        label->setText(placeholder);
        label->setVisibility(imagePath.empty() && !placeholder.empty() ? Visibility::VISIBLE
                                                                       : Visibility::GONE);
        // Same file: only a new download changes it. A different file is
        // stamped by the worker, so moving through the list costs no stat.
        // Still decoding it: that decode stays, a new one would only queue
        // behind it.
        if (imagePath == path) {
            if (path.empty() || loadedStamp == kPending || fileStamp(path) == loadedStamp)
                return;
        }
        path = imagePath;
        loadedStamp = kPending;
        const unsigned wanted = ticket->fetch_add(1) + 1;
        freeTexture();
        if (path.empty())
            return;

        const std::string file = path;
        // No bigger than the frame on screen.
        const int minW = static_cast<int>(std::ceil(frameW * Application::windowScale));
        const int minH = static_cast<int>(std::ceil(frameH * Application::windowScale));
        ASYNC_RETAIN
        async([ASYNC_TOKEN, wanted, file, minW, minH, current = ticket]() {
            // Passed over while queued: the pool has few threads, and a
            // decode nobody waits for would hold up the one that is wanted.
            const int64_t stamp = current->load() == wanted ? fileStamp(file) : 0;
            DecodedImage image = decodeIfWanted(current, wanted, file, minW, minH);
            sync([ASYNC_TOKEN, wanted, image, stamp]() {
                ASYNC_RELEASE
                if (wanted != ticket->load())
                    return;
                loadedStamp = stamp;
                if (!image.pixels)
                    return;
                tex = nvgCreateImageRGBA(Application::getNVGContext(), image.width,
                                         image.height, NVG_IMAGE_NEAREST, image.pixels.get());
                texW = image.width;
                texH = image.height;
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
            nvgFillColor(vg, a(empty));
            nvgFill(vg);
        } else {
            NVGcolor dash = muted;
            dash.a *= 0.3f;
            dashedFrame(vg, x, y, width, height, radius, a(dash));
        }
        Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    float radius;
    float frameW, frameH;
    NVGcolor muted{}, empty{};
    Label* label = nullptr;
    static constexpr int64_t kPending = -1; // decoding: its stamp is not known yet
    std::string path;
    int64_t loadedStamp = 0;
    // Bumped when the frame stops wanting the image being decoded.
    ImageTicket ticket = makeImageTicket();
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

// Follows the right stick; the focus stays on the list, so it never takes it.
class SynopsisScroll : public ScrollingFrame {
  public:
    SynopsisScroll() {
        setFocusable(false);
        setGrow(1);
    }

    View* getDefaultFocus() override { return nullptr; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, Style style,
              FrameContext* ctx) override {
        const float stick = Application::getControllerState().axes[RIGHT_Y];
        if (std::abs(stick) > kStickDeadzone)
            setContentOffsetY(getContentOffsetY() + stick * kStickPixelsPerFrame, false);
        ScrollingFrame::draw(vg, x, y, width, height, style, ctx);
    }
};

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

    factSentence = makeLabel(15);
    facts->addView(factSentence);
    while (factCells.size() < kUsualFacts)
        addFactRow();

    // A long synopsis scrolls inside the room left, so "Fontes" stays in place.
    auto* synopsis = new Box(Axis::COLUMN);
    synopsis->setMarginTop(20);
    synopsis->setMaxWidth(760);
    synopsis->setGrow(1);
    synopsis->setShrink(1);
    synopsisHeading = new BoldLabel();
    synopsisHeading->setFontSize(16);
    synopsisHeading->setText(text("library/details/synopsis"));
    synopsisHeading->setShrink(0);
    synopsis->addView(synopsisHeading);
    synopsisScroll = new SynopsisScroll();
    synopsisScroll->setMarginTop(8);
    auto* synopsisBody = new Box(Axis::COLUMN);
    synopsisBody->setPaddingRight(kSynopsisIndicatorRoom);
    synopsisText = makeLabel(16);
    synopsisBody->addView(synopsisText);
    synopsisScroll->setContentView(synopsisBody);
    synopsis->addView(synopsisScroll);
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
    factSentence->setTextColor(muted);
    static_cast<ArtFrame*>(snapFrame)->applyColors();
    static_cast<ArtFrame*>(titleFrame)->applyColors();
    for (const FactCell& cell : factCells)
        cell.label->setTextColor(muted);
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
    if (!details.identified)
        fileLine->setText(brls::getStr("library/details/not_identified", details.fileName));
    fileLine->setVisibility(details.identified ? Visibility::GONE : Visibility::VISIBLE);
    // With data downloaded, an empty synopsis is a fact, not an invitation.
    synopsisText->setText(!details.synopsis.empty() ? details.synopsis
                          : details.hasData         ? text("library/details/synopsis_empty")
                                                    : text("library/details/synopsis_none"));
    synopsisScroll->setContentOffsetY(0, false);
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

// Two cells side by side, half the width each.
void GameDetailsPanel::addFactRow() {
    auto* row = new Box(Axis::ROW);
    row->setMarginBottom(14);
    const NVGcolor muted = themeColor("equinox/muted");
    for (int c = 0; c < 2; c++) {
        FactCell cell;
        cell.box = new Box(Axis::COLUMN);
        cell.box->setWidthPercentage(50);
        cell.box->setPaddingRight(20);
        cell.label = makeLabel(14);
        cell.label->setTextColor(muted);
        cell.box->addView(cell.label);
        cell.value = makeLabel(16);
        cell.value->setMarginTop(3);
        cell.box->addView(cell.value);
        row->addView(cell.box);
        factCells.push_back(cell);
    }
    facts->addView(row);
    factRows.push_back(row);
}

void GameDetailsPanel::applyFacts() {
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

    if (list.empty())
        factSentence->setText(text(details.hasData ? "library/details/facts_none"
                                                   : "library/details/facts_pending"));
    factSentence->setVisibility(list.empty() ? Visibility::VISIBLE : Visibility::GONE);

    while (factCells.size() < list.size())
        addFactRow();

    for (size_t i = 0; i < factCells.size(); i++) {
        const FactCell& cell = factCells[i];
        // Emptied when hidden: an invisible cell still has a height, and the
        // previous game's long text would make its row taller.
        cell.label->setText(i < list.size() ? text(list[i].label) : "");
        cell.value->setText(i < list.size() ? list[i].value : "");
        // An odd last fact keeps its half width: the empty cell holds the room.
        cell.box->setVisibility(i < list.size() ? Visibility::VISIBLE : Visibility::INVISIBLE);
    }
    for (size_t r = 0; r < factRows.size(); r++)
        factRows[r]->setVisibility(r * 2 < list.size() ? Visibility::VISIBLE : Visibility::GONE);
}

#endif // PLATFORM_SWITCH
