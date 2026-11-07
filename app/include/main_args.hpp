//
//  main_args.hpp
//  Moonlight
//
//  Created by XITRIX on 22.01.2024.
//


#pragma once

#include <string>

void registerDeepLinkHandler();

// False (after telling the player) when the app runs as an applet.
bool canStartApp(int argc, char** argv);
bool startFromArgs(int argc, char** argv);
bool startFromUrl(const std::string& url, bool resetActivityStack = true);
