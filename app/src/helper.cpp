//
//  helper.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 29.05.2021.
//

#include "helper.hpp"

#include <utility>

using namespace brls;

void showAlert(std::string message, const std::function<void(void)>& cb) {
    auto alert = new brls::Dialog(std::move(message));
    alert->addButton("common/close"_i18n, [cb] { cb(); });
    alert->setCancelable(false);
    alert->open();
}

void showError(const std::string& message, const std::function<void(void)>& cb) {
    showAlert("error/dialog_header"_i18n + "\n\n" + message, cb);
}

bool isDialogClosing(brls::Dialog* dialog) {
    // Focus leaves the dialog as soon as its activity starts to pop.
    View* focus = Application::getCurrentFocus();
    return !focus || focus->getParentActivity() != dialog->getParentActivity();
}

brls::Dialog* createLoadingDialog(
    const std::string& text, const std::function<void(void)>& onCancel,
    brls::Label** textLabel, const std::function<void()>& onBackground) {
    Style style = Application::getStyle();
    Box* holder = new Box(Axis::COLUMN);

    auto* label = new Label();
    label->setText(text);
    label->setFontSize(style["brls/dialog/fontSize"]);
    label->setHorizontalAlign(HorizontalAlign::CENTER);
    label->setMarginBottom(21);
    if (textLabel)
        *textLabel = label;

    auto* spinner = new ProgressSpinner(ProgressSpinnerSize::LARGE);
    spinner->View::setSize(Size(92, 92));

    holder->addView(label);
    holder->addView(spinner);

    holder->setAlignItems(AlignItems::CENTER);
    holder->setJustifyContent(JustifyContent::CENTER);
    holder->setPadding(style["brls/dialog/paddingTopBottom"],
                       style["brls/dialog/paddingLeftRight"], 28,
                       style["brls/dialog/paddingLeftRight"]);

    auto* dialog = new Dialog(holder);
    if (onBackground)
        dialog->addButton("common/continue_background"_i18n,
                          [onBackground] { onBackground(); });
    if (onCancel) {
        dialog->addButton("common/cancel"_i18n, [onCancel] { onCancel(); });
    }
    dialog->setCancelable(false);
    const bool buttons = onCancel || onBackground;
    dialog->setFocusable(!buttons);
    dialog->setHideHighlight(!buttons);
    return dialog;
}

bool isHiddenApp(const std::string& name) {
    return name == "Solstice Network Test";
}
