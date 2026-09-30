# Swapping characters

A character's look comes from its mesh (`meshes\chars\<name>\<name>.msh`) and the textures the
mesh references. Replacing the mesh a level loads for a character replaces that character's
model. The skeleton and animations stay the character's own, so swaps between characters with a
compatible skeleton (the humans) animate correctly.

## Worked example: play as Palpatine against Mace Windu

Level `m08_cor_palpatinesoffice`, the story mission where Anakin fights Mace. The player uses
Anakin's hood-down model (`anakinhooddown`). Palpatine's model (`palpatine_grey`) is already in
this level's PAK, because he appears in the scene.

1. **Dump the level** ([dumping assets](dumping-assets.md)) with `map:m08_cor_palpatinesoffice`.
   From `dump\meshes\chars\` you need:
   - `palpatine_grey\palpatine_grey.msh` and `palpatine_grey\palpatine_grey.stx`
   - `anakinhooddown\anakinhooddown.stx`, only as a reference for its internal name

2. **Retarget the mesh's texture references.** A mesh names its textures relative to its own
   folder, and the engine only loads textures that the level's PAK lists. Loaded as
   `anakinhooddown\anakinhooddown.msh`, Palpatine's mesh would look for
   `anakinhooddown\palpatine_grey.stx`, which the level does not have, and render untextured
   (green). So rename the reference to a texture the folder does have, using a name **of the
   same length**. `palpatine_grey` and `anakinhoodDown` are both 14 characters:
   - Replace each texture-reference string `palpatine_grey` in the mesh with `anakinhoodDown`.
   - Leave the longer names that merely start with it, such as
     `palpatine_greyGeo_palpatineBodySG` (these are shader groups).
   - The strings are consecutive and zero-terminated, so never change their length.

3. **Make the texture match.** Save Palpatine's texture as `anakinhooddown.stx`, with the
   internal name (offset `0x40`) copied from Anakin's `anakinhooddown.stx`
   (see [replacing textures](replacing-textures.md)).

4. **Install** both files in `mods\meshes\chars\anakinhooddown\`:
   - `anakinhooddown.msh` (Palpatine's mesh, with the references renamed)
   - `anakinhooddown.stx` (Palpatine's texture, renamed inside)

5. **Run the level.** `logs\swrots.log` shows:

   ```
   Loose resource: d:\meshes\chars\anakinhooddown\anakinhooddown.msh
   Loose resource: d:\meshes\chars\anakinhooddown\anakinhooddown.stx
   PAK: skipping from meshes\chars\anakinhooddown\anakinhooddown.msh to meshes\sharedmaps\specularmap.stx
   PAK: skipping from meshes\chars\anakinhooddown\anakinhooddown.stx to meshes\chars\human.cap
   ```

   The `skipping` lines are expected: Anakin's original mesh and textures sit unused in the PAK
   and are passed over. See [how loading works](how-loading-works.md).

Python used for steps 2 and 3:

```python
import re

msh = bytearray(open("dump/meshes/chars/palpatine_grey/palpatine_grey.msh", "rb").read())
# Whole zero-terminated strings only: this skips palpatine_greyGeo_... names.
for m in re.finditer(rb"(?<=\x00)palpatine_grey(?=\x00)", bytes(msh)):
    msh[m.start():m.end()] = b"anakinhoodDown"
open("mods/meshes/chars/anakinhooddown/anakinhooddown.msh", "wb").write(msh)

stx = bytearray(open("dump/meshes/chars/palpatine_grey/palpatine_grey.stx", "rb").read())
stx[0x40:0x60] = open("dump/meshes/chars/anakinhooddown/anakinhooddown.stx", "rb").read()[0x40:0x60]
open("mods/meshes/chars/anakinhooddown/anakinhooddown.stx", "wb").write(stx)
```

## Current limits

- **Texture names.** The replacement mesh's texture names must be renamed to names that
  already exist in the target folder, at equal length. A tool that rewrites a mesh's string
  table properly would remove this limit.
- **Which characters are available.** A character a level never had is loaded from any PAK on the
  disc that has it (see [how loading works](how-loading-works.md#what-the-port-changes)); that is
  how Yoda fights in a versus arena. Characters that are not on the disc need all of their files
  provided loose, including animations for new skeletons, which is not possible yet.
- **HUD portraits.** These are separate textures (`interfc\hud_b\hud_face_*.stx`) and must be
  swapped separately.

## Which mesh a character uses

The game's character classes register named skins, each with a mesh folder. The table is in the
game executable (see [file formats](file-formats.md#character-skin-table)). For example `Anakin`
uses `Anakin\Anakin`, `Anakin_Hood_Down` uses `AnakinHoodDown\AnakinHoodDown`, and `Palpatine`
uses `Palpatine_grey\palpatine_grey`. Use `LogResources=1` to see which mesh a level actually
loads.
