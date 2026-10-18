# Third-party code and research

MHW VRoid Hunter is licensed under the GNU GPL v3 (see `LICENSE`), because it includes the
GPL-3.0 ooz decompressor. The other bundled libraries are under permissive licenses.

| Component | Use | License |
|---|---|---|
| [ooz](https://github.com/powzix/ooz) (Powzix) | Decompressing the game's Oodle-compressed archive blocks | GPL-3.0-or-later (see `third_party/ooz/NOTICE.md`) |
| [Dear ImGui](https://github.com/ocornut/imgui) v1.91.9 | In-game settings menu | MIT |
| [MinHook](https://github.com/TsudaKageyu/minhook) v1.3.4 | Hooking `IDXGISwapChain::Present` | BSD-2-Clause |
| [cgltf](https://github.com/jkuhlmann/cgltf) v1.15 | Reading .vrm (glTF 2.0) files | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 | Reading the VRM extension data | MIT |
| [stb_image / stb_image_write](https://github.com/nothings/stb) | Decoding VRM textures, preview images | MIT / public domain |

File format knowledge comes from the Monster Hunter World modding community; no code from
these projects is included, but the formats were implemented from their documentation:

* `.mod3` / `.mrl3`: [Mod3-MHW-Importer](https://github.com/AsteriskAmpersand/Mod3-MHW-Importer)
  (AsteriskAmpersand), [MHW-Research 010 templates](https://github.com/TheCrazyT/MHW-Research)
  (CrazyT) and the [MonsterHunterWorldModding wiki](https://github.com/Ezekial711/MonsterHunterWorldModding/wiki)
  (bone function table by Statyk, vertex/weight notes by AsteriskAmpersand and others).
* `.tex`: [MHWTexConvertor](https://github.com/JodoZT/MHWTexConvertor) (Jodo).
* `chunkG*.bin` archives, block keys and key order: [WorldChunkTool](https://github.com/mhvuze/WorldChunkTool)
  (MHVuze; key order found by legendff) and MHWNoChunk. The key order table is stored in
  `data/chunk_keyseq.bin` (packed, two entries per byte).

No Capcom game files are included. The mod reads the hunter skeleton and material templates
from the player's own installation at run time.
