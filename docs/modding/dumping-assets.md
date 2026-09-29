# Dumping assets

The port can save every resource a level loads, under its original development path. This is
the easiest way to get mod source files: the game's own loader does the extraction, so segment
PAKs and Xbox-converted files come out correctly.

1. In `settings.ini`, add:

   ```ini
   [Mods]
   DumpResources=1
   ```

2. Load the level. Put `map:<level>` in `mods\Default_Xbox.cfg` to boot straight into it.
3. Once the level is running, quit. The files are in `dump\`, for example
   `dump\meshes\chars\palpatine_grey\palpatine_grey.msh`.
4. Set `DumpResources=0` again. Dumping slows loading.

Files that already exist in `dump\` are not overwritten. Different levels share many resources,
so the first copy is kept.

## What is and is not dumped

Everything the loader reads while decoding a resource is saved: meshes (`.msh`), textures
(`.stx`), text, xml, csv, audio (`.hwx`) and so on. The bytes are identical to the packed copy.

Not dumped yet:

- **Memory-image resources** (most `.bnm` animations and `.ban` files). The engine loads these
  from a separate block of the PAK instead of reading them one by one.
- **Resources the level never loads.** Dump each level whose content you need.

Set `[Debug] LogResources=1` to see which resources a level loads, and in what order.
