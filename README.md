# MHW VRoid Hunter

Play **Monster Hunter: World** (Steam) as your own **VRoid / VRM** model.

* Drop your `.vrm` file into the **`VRoidHunter` folder** in the game folder; it is converted
  automatically when the game starts (in a separate process, never inside the game).
* The model replaces your hunter and is **sized to your hunter's height automatically**.
* Per armor part (head, chest, arms, waist, legs) you can choose to **show your equipped hunter
  armor**, fitted onto the VRoid body.
* **Client side only**: nothing is sent to other players and the game's own archives are never
  modified. Other players see your normal hunter.
* **One-click install**: run `MHWVroidHunterSetup.exe`, or drop two files into the game folder.

> **Status:** the converter, file formats, archive reader, installer and the in-game menu are
> built and tested here (see [Testing](#testing)), but this project has **not yet been run
> against the real game** because the game isn't available in the build environment. The
> first real-game test is the next step — see [Known limitations](#known-limitations).

## Install

**Easy:** download the release zip, run `MHWVroidHunterSetup.exe`, click **1. Install mod**,
put your `.vrm` into the `VRoidHunter` folder that appears in the game folder (**Open models
folder** button), then start the game. **2. Add VRoid model...** copies a file into that folder
and converts it immediately.

**Manual:** copy the contents of `manual-install/` into the folder that contains
`MonsterHunterWorld.exe`:

```
Monster Hunter World\dinput8.dll                                              (small loader)
Monster Hunter World\nativePC\plugins\MHWVroidHunter.dll                      (the mod)
Monster Hunter World\nativePC\plugins\MHWVroidHunter\VRoidHunterConverter.exe (the converter)
Monster Hunter World\VRoidHunter\                                             (put your .vrm here)
```

If you already use **Stracker's Loader**, skip `dinput8.dll`; Stracker's Loader loads the plugin.
In that case the game does not wait for a first-time conversion, so restart it once the
converter window has closed.

### The drop folder

* Any `.vrm` in `<game>\VRoidHunter\` is picked up when the game starts. If there are several,
  the **newest** file is used; the F7 menu can switch between them (until a newer file is dropped
  in).
* Conversion runs in `VRoidHunterConverter.exe`, a separate process. At start-up our
  `dinput8.dll` holds the game until it finishes, so the model is ready the first time; a small
  window shows progress. Unchanged models are skipped in well under a second.
* You can also run `VRoidHunterConverter.exe --apply` yourself before starting the game.

**Steam Deck / Proton:** set the launch options to `WINEDLLOVERRIDES="dinput8=n,b" %command%`.

**Uninstall:** the installer's **Uninstall** button (or **Remove mod files** in the F7 menu)
deletes every file the mod created and restores a `dinput8.dll` it replaced.

## Using it

Press **F7** in game:

| Setting | What it does |
|---|---|
| Play as my VRoid model | Turns the model swap on or off. |
| Model | Chooses between the `.vrm` files in the `VRoidHunter` folder (VRM 0.x and 1.0). **Open models folder** opens it. |
| Show armor: Head / Chest / Arms / Waist / Legs | Checked parts show your equipped (or layered) hunter armor, unchecked parts show the VRoid model. |
| Body fit | 100% (default) moves the VRoid's joints onto the hunter's joints so armor and animations line up. Lower keeps more of the VRoid's own proportions. |
| Advanced | Male/female hunter, texture size, removal of fully transparent parts. |
| Apply | Runs the converter in the background (outside the game). Then change equipment, enter your room or travel so the game reloads your hunter. |

In DirectX 12 mode the menu opens as a separate small window instead of an overlay.

## How it works

The game loads loose files from `nativePC` in place of the files in its archives. The mod uses
that, so it never patches game code or memory:

1. **Read the hunter from your own game files.** The `chunkG*.bin` archives are decrypted and
   decompressed (open-source Oodle-compatible decoder) to read the player's armor models. A
   complete armor set provides the hunter skeleton (bone ids, rest pose), mesh settings and an
   armor material to use as a template. No Capcom files are shipped with the mod.
2. **Fit the VRM to the hunter** (`src/core/retarget.cpp`):
   * The game's axis conventions (up, forward, handedness) are measured from the skeleton
     itself, so nothing about the coordinate system is assumed.
   * The model is uniformly scaled so its ankle-to-head height equals the hunter's.
   * Each VRM humanoid bone is mapped to the hunter bone with the same job (bone function ids
     from the MHW modding wiki), rotated onto the hunter's bone direction (T-pose → hunter bind
     pose) and stretched so its joints land on the hunter's joints (the *Body fit* setting).
   * Skin weights are moved onto the hunter bones. Non-humanoid bones (hair, skirt, accessories)
     follow their closest humanoid parent.
   * Triangle winding and tangent sign conventions are measured on the game's own meshes and
     matched.
3. **Split into armor slots.** Every triangle is assigned to head / chest / arms / waist / legs
   by its bones, so each part can independently be the VRoid body or your armor.
4. **Write game files**: `.mod3` models (vertex/weight/bounding box layout per the community
   documentation), `.mrl3` materials (cloned from an armor material, with our textures bound)
   and `.tex` textures (BC1 compressed with mip maps).
5. **Install by hard links.** Every armor model path for the parts showing the VRoid (all armor
   sets, both genders) becomes a hard link to the single converted file, so hundreds of armor
   paths cost no extra disk space. When the VRoid head is shown, the hunter's face and hair
   models are replaced with an invisible model. Files that another mod already provides are
   never overwritten, and a manifest records everything the mod created so it can be removed
   cleanly.

### Why the settings menu is an overlay

Adding an entry to Capcom's own Options screen would require patching the game's UI code and
UI layout files for one specific game build. The F7 overlay gives the same settings without
touching game code, so it can't break with game patches or conflict with UI mods.

## Known limitations

These are expected from the approach or still need confirmation in the real game:

* **Not yet verified in game.** Formats were implemented from community documentation and
  cross-checked with an independent parser, but details such as the chosen template material,
  bounding boxes and texture formats still need a real-game test. If the model shows as a
  checkerboard or does not appear, please open an issue with
  `nativePC/plugins/MHWVroidHunter/last_build_report.txt` and `log.txt` attached.
* **No physics:** VRM spring bones (hair, skirt) move rigidly with the closest body bone.
* **No facial expressions:** the VRoid face does not blink or talk.
* **Materials:** a standard armor shader is used (no toon shading). Semi-transparent parts are
  cut out at 50% alpha; whether the armor shader honours cut-out alpha still needs checking.
* **Hide head armor option:** if the game's "hide head armor" display option is on, the VRoid
  head (which lives in the head slot) is hidden too.
* **Armor physics files** of individual armor pieces refer to bones that the converted model
  doesn't have; per the modding wiki those bones then simply don't move.
* **VRM 1.0 node constraints** (e.g. the extra robot arm of the "Seed-san" sample) are ignored.
* The game may need to reload your hunter (or a restart) before changes appear.

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, and MinGW-w64 for the Windows binaries.

```sh
# Linux build + tests (converter core and command line tool)
cmake -B build && cmake --build build -j && ./build/vh_tests

# Windows binaries (plugin DLL, dinput8 loader, installer, CLI) cross-compiled from Linux
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake && cmake --build build-win -j

# Release zip (dist/MHWVroidHunter-<version>.zip)
scripts/package.sh 1.0.0
```

GitHub Actions (`.github/workflows/build.yml`) builds, tests and uploads the zip for every push;
pushing a `v*` tag also publishes it as a GitHub release.

### Command line tool

`vroidhunter-cli` exposes the same pipeline for modders:

```
vroidhunter-cli apply   --game "<MHW folder>" --vrm model.vrm [--show-armor helm,body] [--conform 1]
vroidhunter-cli remove  --game "<MHW folder>"
vroidhunter-cli convert --templates <extracted chunk folder> --out <nativePC folder> --vrm model.vrm
vroidhunter-cli preview --vrm model.vrm --png preview.png      # software render of the fitted model
vroidhunter-cli inspect file.mod3|file.mrl3|file.tex
vroidhunter-cli list-chunk chunkG0.bin
```

The installer also has a command line: `MHWVroidHunterSetup.exe --install|--uninstall <game folder>`
and `--convert <game folder> <model.vrm> [--show-armor]` (progress goes to `setup_log.txt`).

## Testing

What has been verified without the game:

* `tests/test_main.cpp` (`vh_tests`) generates a VRM 0.x and a VRM 1.0 test avatar, fake hunter
  armor files and a fake encrypted archive, then checks: mod3/mrl3 round trips (including meshes
  past 65,535 vertices), BC1 encoding, archive decryption across block boundaries, that hands
  land on the hunter's hands, height scaling, a mirrored (left-handed) game skeleton, transparent
  triangle removal, slot splitting, hard links, armor toggles, never overwriting another mod's
  files, and clean removal.
* `tests/crosscheck_reference.py` parses generated `.mod3` files with AsteriskAmpersand's
  reference importer code; all counts, face ranges, weights, bone ids and offsets agree.
* The decompressor was checked against the ooz test corpus (Kraken/Mermaid/Selkie/Leviathan).
* Real VRoid-style avatars (VRM 0.x "Alicia Solid", VRM 1.0 "Seed-san") were converted and
  rendered with `vroidhunter-cli preview` to check the fit visually.
* The Windows build was run under Wine: Japanese file paths, hard links, the installer
  (install / convert / uninstall with backup and restore of an existing `dinput8.dll`), and a
  stand-in game (`tests/fakegame`) that loads our `dinput8.dll` like MHW does, where the plugin
  hooked DirectX 11 and drew the menu. With a model dropped into `VRoidHunter\`, the stand-in
  game waited at start-up while `VRoidHunterConverter.exe` converted it from a fake `chunkG0.bin`
  (`tests/make_fake_chunk.py`), the model files existed when the game continued, and the next
  start skipped the work.

## License

GPL-3.0 (see `LICENSE` and `THIRD_PARTY_NOTICES.md`). Monster Hunter is a trademark of Capcom;
this project is not affiliated with or endorsed by Capcom, VRoid or pixiv.
