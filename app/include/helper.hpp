//
//  helper.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 29.05.2021.
//

#pragma once

#include <borealis.hpp>

void showAlert(
    std::string message, const std::function<void(void)>& cb = [] {});
void showError(
    const std::string& message, const std::function<void(void)>& cb = [] {});
// True once the user has started closing the dialog (its button was pressed
// and the fade-out runs): dismissing it again would pop the screen twice.
bool isDialogClosing(brls::Dialog* dialog);

// textLabel, if given, receives the dialog's label so the text can change
// while it is open. onBackground, if given, adds a "Continue in background"
// button before Cancel: it closes the dialog and the work goes on.
brls::Dialog* createLoadingDialog(
    const std::string& text,
    const std::function<void(void)>& onCancel = nullptr,
    brls::Label** textLabel = nullptr,
    const std::function<void()>& onBackground = nullptr);

// Apps the host lists that are not games (Solstice's network test): hidden
// from the library, favorites and the game counts.
bool isHiddenApp(const std::string& name);
