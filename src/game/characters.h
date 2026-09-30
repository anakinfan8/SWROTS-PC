#pragma once

namespace swrots::game {

// A character's variant (costume) whose model is on the disc: `preferred` when its model is, else the
// first that is (a class's first variant is not always shipped: the battle droid's plain
// "BattleDroid" model is not, its "hordeBattleDroid" is), else `preferred`. `character` is a created
// character object; its variant list is at +0x1E0.
int VariantOnDisc(const void* character, int preferred);

} // namespace swrots::game
