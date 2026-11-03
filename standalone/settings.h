// Settings — what the standalone app remembers between runs, kept as a
// flattened BMessage in ~/config/settings/NAMku/standalone:
//   "backend"         string  "mediakit" or "jack"
//   "input:mediakit"  string  the capture source last chosen on the Media Kit
//   "input:jack"      string  the capture source last chosen on JACK
//   "frame"           BRect   the window's frame
//   "component"       B_RAW_TYPE  the plug-in's processor state
//   "controller"      B_RAW_TYPE  the plug-in's controller state
// The file is read once at start and written when the window closes.

#pragma once

#include <Message.h>

namespace NAMku
{

// The settings file's contents; an empty message if it is missing or unreadable.
BMessage LoadSettings();
// Writes the settings file (directories created as needed).
status_t SaveSettings(const BMessage &settings);

} // namespace NAMku
