#pragma once

namespace swrots::game {

// The versus roster beyond the game's nine fighters: extra select-screen slots (from 10; 9 is the
// game's Random) for characters on the disc that were never versus fighters. At every boot.
void InstallRoster();

// Copies the game's nine fighter classes (kDuelistClasses) into the roster's table, which the duel
// reads by slot; after every change to the game's table (the duelist command).
void SyncRosterClasses();

// The duelist whose duel cameras the extra fighter in `slot` uses (e.g. "Anakin", for
// cinematics\introcamera\Anakin_Intro_Cam.cin), or null for a slot that is not an extra fighter's.
const char* ExtraFighterCameras(int slot);

} // namespace swrots::game
