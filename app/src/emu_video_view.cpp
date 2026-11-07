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

class EmuVideoView : public Box {
  public:
    explicit EmuVideoView(const emu::ConsolePaths& paths)
        : Box(Axis::COLUMN), path(emu::videoDefaultsPath(paths)),
          current(emu::readVideoDefaults(path)) {
        getAppletFrameItem()->title = "emu/video/title"_i18n;
        setPadding(30, 80, 30, 80);

        addRow("emu/video/size"_i18n,
               {"emu/video/size_original"_i18n, "emu/video/size_8_7"_i18n,
                "emu/video/size_4_3"_i18n, "emu/video/size_full"_i18n},
               emu::kSizeIds, &emu::VideoDefaults::size);
        addRow("emu/video/smoothing"_i18n,
               {"emu/video/smooth_pixel"_i18n, "emu/video/smooth_sharp"_i18n,
                "emu/video/smooth_soft"_i18n},
               emu::kSmoothIds, &emu::VideoDefaults::smooth);
        addRow("emu/video/filter"_i18n,
               {"emu/video/filter_none"_i18n,
                "emu/video/filter_scanlines"_i18n,
                "emu/video/filter_ntsc"_i18n},
               emu::kFilterIds, &emu::VideoDefaults::filter);
        border = addRow("emu/video/border"_i18n,
                        {"emu/video/border_equinox"_i18n,
                         "emu/video/border_cover"_i18n,
                         "emu/video/border_tv"_i18n,
                         "emu/video/border_black"_i18n},
                        emu::kBorderIds, &emu::VideoDefaults::border);
        updateBorder();
    }

  private:
    const std::string path;
    emu::VideoDefaults current;
    SelectorCell* border = nullptr;

    SelectorCell* addRow(const std::string& title,
                         const std::vector<std::string>& labels,
                         const std::vector<std::string>& ids, Field field) {
        auto* cell = new SelectorCell();
        const std::vector<std::string>* idList = &ids;
        cell->init(title, labels, indexOf(ids, current.*field),
                   [this, cell, idList, field](int selected) {
                       if (selected < 0 || selected >= (int)idList->size())
                           return;
                       emu::VideoDefaults next = current;
                       next.*field = (*idList)[selected];
                       if (!emu::writeVideoDefaults(path, next)) {
                           Application::notify("emu/video/save_failed"_i18n);
                           cell->setSelection(indexOf(*idList, current.*field),
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
