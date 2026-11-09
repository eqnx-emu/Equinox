//
//  game_details_panel.hpp
//  Equinox
//
//  The right-hand column of the game library: name, the download action,
//  the gameplay and title screen images, the fact sheet and the synopsis.
//

#pragma once

#include "emu/GameDataJob.hpp"
#include "emu/GameDb.hpp"
#include "emu/GameMeta.hpp"
#include <borealis.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GameDetails {
    std::string name, fileName;
    bool identified = false;
    bool hasData = false;
    const emu::GameInfo* info = nullptr; // null when not identified
    std::vector<std::string> regions;    // noIntroRegions(canonical); empty when not identified
    std::string snapPath, titlePath;     // empty when the file does not exist
    // Tells "libretro has no such image" (Missing) from "never downloaded".
    emu::PartState snap = emu::PartState::Unknown, title = emu::PartState::Unknown;
    std::string synopsis; // empty shows the "no synopsis" text
};

class GameDetailsPanel : public brls::Box {
  public:
    GameDetailsPanel();

    // animate: a different game, so the content fades in once and the
    // progress and failure of the previous game are dropped (call
    // setProgress/setFailed afterwards). False: an update of the same game.
    using brls::Box::show; // the borealis overloads stay reachable
    void show(const GameDetails& details, bool animate);
    // No step and not queued: this game is not downloading.
    void setProgress(std::optional<emu::DataStep> step, bool queued);
    // Cancelled and None show nothing, like no value. Starting a download
    // (setProgress with a step or queued) also clears the failure.
    void setFailed(std::optional<emu::DataStop> stop);

    // "Baixar dados deste jogo", "Atualizar" and "Tentar de novo".
    std::function<void()> onDownload;

    void willAppear(bool resetState = false) override;

  private:
    enum class Action { Missing, Queued, Downloading, Failed, Ready };

    GameDetails details;
    std::optional<emu::DataStep> step;
    bool queued = false;
    std::optional<emu::DataStop> failed;

    brls::Box* content = nullptr;
    brls::Label* title = nullptr;
    brls::Label* fileLine = nullptr;
    brls::Button* downloadButton = nullptr;
    brls::Box* queuedRow = nullptr;
    brls::Label* waitingLabel = nullptr;
    brls::Label* queuedLabel = nullptr;
    brls::Box* progressBox = nullptr;
    brls::Label* stepLabel = nullptr;
    brls::Label* percentLabel = nullptr;
    brls::View* bar = nullptr;
    brls::Box* failedRow = nullptr;
    brls::Label* failedLabel = nullptr;
    brls::Button* retryButton = nullptr;
    brls::Box* readyRow = nullptr;
    brls::Label* readyLabel = nullptr;
    brls::Button* updateButton = nullptr;
    brls::View* snapFrame = nullptr;
    brls::View* titleFrame = nullptr;
    brls::Label* titleCaption = nullptr;
    brls::Box* facts = nullptr;
    // Made once and filled in: rebuilding them on every game was most of
    // the cost of moving through the list.
    brls::Label* factSentence = nullptr; // "no facts" / "not downloaded"
    struct FactCell {
        brls::Box* box = nullptr;
        brls::Label* label = nullptr;
        brls::Label* value = nullptr;
    };
    std::vector<brls::Box*> factRows;   // two cells each
    std::vector<FactCell> factCells;
    brls::Label* synopsisHeading = nullptr;
    brls::ScrollingFrame* synopsisScroll = nullptr;
    brls::Label* synopsisText = nullptr;
    brls::Label* sources = nullptr;

    brls::Animatable fade = 1.0f;

    void applyColors();
    void applyDetails();
    void applyAction();
    void applyMedia();
    void applyFacts();
    void startFade(bool animate);
};
