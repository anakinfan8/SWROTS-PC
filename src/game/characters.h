#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swrots::game {

// Characters: which class the player is, in which costume or mesh. At every boot, after the image is
// loaded.
void InstallCharacters();

// The game's own string naming a registered character class (case-insensitive), or null.
const char* RegisteredClassName(const char* name);

// Makes the player a character class (e.g. "ICloneTrooper") from the next level start until the game
// is closed; null goes back to each level's own player (and its costume and mesh). False when the game
// knows no such class.
bool SetPlayerClass(const char* className);

// The class set with SetPlayerClass, or null.
const char* PlayerClass();

// The player's costume from the next level start: a variant's name, a part of its name found in no
// other ("duel" for Anakin_Duel) or its number; empty for the usual one.
void SetPlayerVariant(const std::string& variant);
std::string PlayerVariantChoice();

// The player's mesh from the next level start, under meshes\chars without the extension (e.g.
// "anakinduel\anakinduel"), in place of its costume's; empty for the costume's own.
void SetPlayerMesh(const std::string& mesh);
std::string PlayerMesh();

// The player's texture set from the next level start ("Starting texture set" in the game's level
// data, e.g. the clone trooper's _var01): its number (0 the plain textures) or name; empty for the
// usual one.
void SetPlayerSkin(const std::string& skin);
std::string PlayerSkin();

// A class's texture sets (e.g. "_var01", "_var02"; the plain textures, set 0, not listed), and one by
// number or name (with or without its "_"); -1 when it has none such.
std::vector<std::string> ClassTextureSets(const char* className);
int ClassTextureSetIndex(const char* className, const std::string& spec);

// A character class's costumes, from the game's static lists.
struct Variant {
    const char* name;
    const char* mesh; // under meshes\chars, e.g. "AnakinDuel\AnakinDuel"
    bool onDisc;
};
std::vector<Variant> ClassVariants(const char* className);

// A class's costume by number, name or a unique part of its name; -1 when none (or several) match.
int ClassVariantIndex(const char* className, const std::string& spec);

// The game's costume list of a class (its name without the leading I), or null.
const uint8_t* FindVariantList(const char* className);

// The registered character classes that have costumes (all registered classes with `all`), sorted;
// empty until the game has registered them.
std::vector<std::string> CharacterClasses(bool all);

// True once a level's player was created since the last boot (a level is running).
bool PlayerInLevel();

// Whether a change of character restarts the running mission at once ([Debug] AutoRestart, on by
// default); otherwise it applies from the next level start.
void SetRestartOnChange(bool enabled);
bool RestartOnChange();

// The character meshes in the disc's PAKs ("folder\file", lower case), those containing `filter`.
std::vector<std::string> CharacterMeshes(const std::string& filter);

// A mesh named by its folder, its file or "folder\file" (with or without meshes\chars and .msh),
// as "folder\file"; empty with `error` set when none or several match. A "folder\file" the disc does
// not have is returned as it is (a loose copy under mods\ may provide it).
std::string ResolveMesh(const std::string& text, std::string& error);

// A character's variant (costume) whose model is on the disc: `preferred` when its model is, else the
// first that is (a class's first variant is not always shipped: the battle droid's plain
// "BattleDroid" model is not, its "hordeBattleDroid" is), else `preferred`. `character` is a created
// character object; its variant list is at +0x1E0.
int VariantOnDisc(const void* character, int preferred);

} // namespace swrots::game
