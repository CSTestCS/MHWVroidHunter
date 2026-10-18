MHW VRoid Hunter - play Monster Hunter: World (Steam) as your VRoid model
=========================================================================

INSTALL
  1. Close the game.
  2. Run MHWVroidHunterSetup.exe and click "1. Install mod"
     (the game folder is found through Steam; use "Browse..." if it is not).
  3. Put your .vrm file (exported from VRoid Studio) into the new "VRoidHunter" folder
     inside the game folder. The installer's "Open models folder" button opens it.
     ("2. Add VRoid model..." does the same and converts the model right away.)
  4. Click "2. Add VRoid model..." (or start the game, press F7 and click Apply).
     The model is converted once (a few seconds). The very first time, the mod also reads
     the hunter's skeleton from the game files once and saves it; it never does that again.
  5. Start the game. Nothing is converted at start-up.

INSTALL WITHOUT THE INSTALLER
  Copy everything inside "manual-install" into the Monster Hunter World folder (the folder
  with MonsterHunterWorld.exe), so you get:
      Monster Hunter World\dinput8.dll
      Monster Hunter World\nativePC\plugins\MHWVroidHunter.dll
      Monster Hunter World\nativePC\plugins\MHWVroidHunter\VRoidHunterConverter.exe
      Monster Hunter World\VRoidHunter\            <- put your .vrm here
  If you already use Stracker's Loader, skip dinput8.dll.
  Then start the game, press F7 and click Apply.

CHANGING MODELS
  Drop another .vrm into the VRoidHunter folder (the newest file is used, or pick one in the
  F7 menu) and click Apply. The corner message at start-up tells you if Apply is needed.

IN GAME (F7)
    - Play as my VRoid model ...... turn the mod on/off
    - Model ....................... choose between the .vrm files in the folder
    - Show armor (Head/Chest/Arms/Waist/Legs)
                                    show your hunter's armor fitted to the VRoid body
    - Body fit .................... 100% = joints moved onto the hunter's (best fit)
    - Apply ....................... converts in the background (outside the game)
  After Apply, change equipment, enter your room or travel so the game reloads your hunter.
  In DirectX 12 mode, F7 opens the menu as a separate window.

CONVERT WITHOUT STARTING THE GAME
  Run nativePC\plugins\MHWVroidHunter\VRoidHunterConverter.exe --apply

UNINSTALL
  MHWVroidHunterSetup.exe -> "Uninstall" (or "Remove mod files" in the F7 menu). Every file
  the mod created is removed. Your .vrm files in the VRoidHunter folder are kept.

STEAM DECK / LINUX (Proton)
  Launch options:  WINEDLLOVERRIDES="dinput8=n,b" %command%

PROBLEMS
  Logs: Monster Hunter World\nativePC\plugins\MHWVroidHunter\
        (log.txt, converter_status.txt, last_build_report.txt)
