# Replacing textures

Textures are `.stx` files (see [file formats](file-formats.md#stx-texture)). To replace one:

1. Find the texture's path, either with `LogResources=1` or by [dumping](dumping-assets.md)
   the level.
2. Put the new file at the same path under `mods\`.
3. Make sure the name stored inside the file (32 bytes at offset `0x40`) is the name of the
   texture being replaced.

## The embedded-name rule

The engine registers a texture under the name stored inside the STX file, not under its file
name. A texture copied from elsewhere keeps its old internal name, so the game cannot find it
and crashes or shows the wrong texture.

For example, replacing Vader's HUD portrait with another portrait:

- File: `mods\interfc\hud_b\hud_face_vader01.stx`
- Internal name at `0x40`: must be `HUD_Face_Vader01` (copy it from the original), padded with
  zero bytes to 32 bytes.

Python snippet to set the name:

```python
data = bytearray(open("new.stx", "rb").read())
original = open("dump/interfc/hud_b/hud_face_vader01.stx", "rb").read()
data[0x40:0x60] = original[0x40:0x60]
open("mods/interfc/hud_b/hud_face_vader01.stx", "wb").write(data)
```

The replacement's size, dimensions and format may differ from the original.
