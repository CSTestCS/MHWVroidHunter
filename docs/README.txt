MHW VRoid Hunter - play Monster Hunter: World (Steam) as your VRoid model
=========================================================================

INSTALL (easy way)
  1. Close the game.
  2. Run MHWVroidHunterSetup.exe.
  3. Click "1. Install mod" (the game folder is found automatically through Steam;
     use "Browse..." if it is not).
  4. Click "2. Choose VRoid model..." and pick your .vrm file (export it from VRoid Studio).
     The first conversion reads the game's archives and can take a few minutes.
  5. Start the game.

INSTALL (manual way)
  Copy everything inside the "manual-install" folder into the Monster Hunter World folder
  (the folder that contains MonsterHunterWorld.exe), so you end up with:
      Monster Hunter World\dinput8.dll
      Monster Hunter World\nativePC\plugins\MHWVroidHunter.dll
  If you already use Stracker's Loader, only copy nativePC\plugins\MHWVroidHunter.dll.
  Start the game, press F7 and choose your model in the menu.

IN GAME
  Press F7 to open the "VRoid Hunter settings" menu:
    - Play as my VRoid model ...... turn the mod on/off
    - Choose .vrm file ............ pick or change the model
    - Show armor (Head/Chest/Arms/Waist/Legs)
                                    show your hunter's equipped armor, fitted to the
                                    VRoid body, for each part
    - Body fit .................... 100% = VRoid joints moved onto the hunter's joints
                                    (best for armor and animations)
    - Apply ....................... builds/installs the files
  After pressing Apply, change equipment, enter your room or travel so the game reloads
  your hunter. If you play in DirectX 12 mode, F7 opens the menu as a separate window.

UNINSTALL
  Run MHWVroidHunterSetup.exe and click "Uninstall" (or press "Remove mod files" in the
  F7 menu). Every file the mod created is removed; nothing in the game's own archives is
  ever modified.

STEAM DECK / LINUX (Proton)
  Add this to the game's Steam launch options so our dinput8.dll is used:
      WINEDLLOVERRIDES="dinput8=n,b" %command%

NOTES
  - Everything is client side: other players see your normal hunter.
  - Do not use the in-game "hide head armor" option while the VRoid head is shown -
    the game then hides the head slot, which is where the VRoid head lives.
  - VRoid hair/skirt physics (spring bones) do not move; they follow the closest body bone.
  - Files and logs: Monster Hunter World\nativePC\plugins\MHWVroidHunter\
