//
//  emu_video_view.cpp
//  Equinox
//

#include "emu_video_view.hpp"
#include "emu/VideoDefaults.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace brls;
using namespace brls::literals;

namespace {

using Field = std::string emu::VideoDefaults::*;

int indexOf(const std::vector<std::string>& ids, const std::string& id) {
    auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? 0 : (int)(it - ids.begin());
}

// One label per id, so dropping an id also drops its label:
// "emu/video/<row>_<id>", with the ':' of ratios such as "8:7" as '_'.
std::vector<std::string> labelsFor(const std::string& row,
                                   const std::vector<std::string>& ids) {
    std::vector<std::string> labels;
    for (std::string id : ids) {
        std::replace(id.begin(), id.end(), ':', '_');
        labels.push_back(brls::getStr("emu/video/" + row + "_" + id));
    }
    return labels;
}

class EmuVideoView : public Box {
  public:
    explicit EmuVideoView(const emu::ConsolePaths& paths)
        : Box(Axis::COLUMN), path(emu::videoDefaultsPath(paths)),
          current(emu::readVideoDefaults(path)) {
        getAppletFrameItem()->title = "emu/video/title"_i18n;
        setPadding(30, 80, 30, 80);

        addRow("emu/video/size"_i18n, "size", emu::kSizeIds,
               &emu::VideoDefaults::size);
        addRow("emu/video/smoothing"_i18n, "smooth", emu::kSmoothIds,
               &emu::VideoDefaults::smooth);
        addRow("emu/video/filter"_i18n, "filter", emu::kFilterIds,
               &emu::VideoDefaults::filter);
        border = addRow("emu/video/border"_i18n, "border", emu::kBorderIds,
                        &emu::VideoDefaults::border);
        updateBorder();
    }

  private:
    const std::string path;
    emu::VideoDefaults current;
    SelectorCell* border = nullptr;

    // `row` names the label keys, see labelsFor.
    SelectorCell* addRow(const std::string& title, const std::string& row,
                         std::vector<std::string> ids, Field field) {
        auto* cell = new SelectorCell();
        cell->init(title, labelsFor(row, ids), indexOf(ids, current.*field),
                   [this, cell, ids, field](int selected) {
                       if (selected < 0 || selected >= (int)ids.size())
                           return;
                       emu::VideoDefaults next = current;
                       next.*field = ids[selected];
                       if (!emu::writeVideoDefaults(path, next)) {
                           Application::notify("emu/video/save_failed"_i18n);
                           cell->setSelection(indexOf(ids, current.*field),
                                              true);
                           return;
                       }
                       current = next;
                       updateBorder();
                   });
        addView(cell);
        return cell;
    }

    // The border is hidden in full screen, so it cannot change there.
    void updateBorder() {
        const bool enabled = current.size != "full";
        Theme theme = Application::getTheme();
        border->setFocusable(enabled);
        border->setActionAvailable(BUTTON_A, enabled);
        border->setTextColor(enabled ? theme["brls/text"]
                                     : theme["brls/text_disabled"]);
        border->setDetailTextColor(
            enabled ? theme["brls/list/listItem_value_color"]
                    : theme["brls/text_disabled"]);
    }
};

} // namespace

View* makeEmuVideoView(const emu::ConsolePaths& paths) {
    return new EmuVideoView(paths);
}
