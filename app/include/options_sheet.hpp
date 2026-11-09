//
//  options_sheet.hpp
//  Equinox
//
//  A list of actions in a panel that slides in from the left, over the
//  dimmed screen. B or a touch outside closes it; picking a row closes it
//  and then runs that row.
//

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

class OptionsSheet : public brls::Box {
  public:
    struct Item {
        std::string label;
        std::function<void()> action;
    };

    static void open(const std::string& title, std::vector<Item> items);

    void show(std::function<void(void)> cb, bool animate,
              float animationDuration) override;
    bool isTranslucent() override { return true; }

  private:
    OptionsSheet(const std::string& title, std::vector<Item> items);

    brls::Box* panel = nullptr;
    brls::Animatable slide = 0.0f;
};
