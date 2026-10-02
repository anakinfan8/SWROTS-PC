#pragma once

namespace swrots::game {

// Characters: which class the player is, by name. At every boot, after the image is loaded.
void InstallCharacters();

// The game's own string naming a registered character class (case-insensitive), or null.
const char* RegisteredClassName(const char* name);

// Makes the player a character class (e.g. "ICloneTrooper") from the next level start until the game
// is closed; null goes back to each level's own player. False when the game knows no such class.
bool SetPlayerClass(const char* className);

// The class set with SetPlayerClass, or null.
const char* PlayerClass();

// A character's variant (costume) whose model is on the disc: `preferred` when its model is, else the
// first that is (a class's first variant is not always shipped: the battle droid's plain
// "BattleDroid" model is not, its "hordeBattleDroid" is), else `preferred`. `character` is a created
// character object; its variant list is at +0x1E0.
int VariantOnDisc(const void* character, int preferred);

} // namespace swrots::game
