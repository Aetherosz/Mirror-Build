# Mirror Build 0.3.6: in-game test

**Use a test save.** Keep AutoLink off (see Known).

**Key: L** while holding a building with an input or output: production, extractors, generators, storage, truck and drone stations, and modded machines of those kinds. Belts, pipes, logistics, poles, walls, decor, freight platforms and the special buildings (Space Elevator, AWESOME Sink, Portal, gift tree) ignore it.

New in 0.3: the text on mirrored buildings reads the right way round. That covers the FICSIT logos and the words made of letters, like SM3LT3R-700, PAC-IT and CON-STRUCT-0R 01.

1. **Text:** mirror a Smelter, Constructor, Packager, Foundry, Coal Generator, Biomass Burner and Refinery. Logos and words read normally. Check the lit words aren't darker than on a normal one. Put a plain one beside each and check nothing changed on it.
2. **Build animation:** watch the text while one builds and while one is dismantled. It should animate like the rest of the building.
3. **Machines:** mirror an Assembler, Blender and Manufacturer. Flipped, inputs still at the input end, items and fluid move.
4. **Generator:** mirror a Coal Generator. Fuel, water and power work.
5. **Miner:** mirror a Mk.1 on a node. Build a second one straight after and check it faces the same way. Then upgrade the first to Mk.2: it must stay mirrored, facing the same way, still feeding its belt.
6. **Extractors:** Water Extractor and Oil Extractor. They place and produce.
7. **Storage:** stack a mirrored container on a normal one. Inputs above inputs. Fluid Buffer connects.
8. **Truck Station:** a vehicle docks, loads and unloads.
9. **Drone Port:** ports stay on their side, swapped along it. A drone lands and unloads.
10. **Leave alone:** L does nothing while holding a freight platform, Space Elevator, AWESOME Sink, splitter, belt, pipe, pump, power pole or wall.
11. **Save and reload:** everything above is still mirrored, readable and working.
12. **Blueprints (new in 0.3.6):** in a Blueprint Designer, build a mirrored Assembler, a mirrored Refinery and a mirrored Coal Generator, each with a plain one beside it and belts or pipes between them. Save the blueprint. A file `<name>.sbpmirror` appears next to its `.sbp`.
    - Place the blueprint, turned a different way than the designer. Mirrored ones come out mirrored (ports and text as in the designer), plain ones plain, belts and pipes still connected, items and fluid flow.
    - Look at the hologram before placing. Note whether the mirrored ones already show mirrored there.
    - Load the blueprint back into an empty designer: the mirrors stay.
    - Save it again with nothing mirrored: the `.sbpmirror` file goes away.
    - Blueprints saved before 0.3.6 have no `.sbpmirror` and still come out back to front. Save them again from a designer.

0.3.1: mirrored Foundry, Coal Generator and other instanced machines were dark (0.1 to 0.3). They now draw on their own, like the Refinery. Check a mirrored Foundry and Coal Generator look as bright as plain ones, with global illumination on and off, in a fresh build and after loading the save.

0.3.5: mirrored machines' meshes are taken out of shared instancing, legs and feet drawn as plain meshes.

Known:
- Holograms look darker while mirrored. This is a known lighting bug, not a feature: there is no quick fix, so it is left in for now. A side effect is that you can tell at a glance when a hologram is mirrored.
- One small dark spot on one logo of a mirrored Converter.
- AutoLink crashes on pipe junctions (believed to be an AutoLink issue; AutoLink is unmaintained). Not yet confirmed with Mirror Build removed. Keep it off while testing.
- Multiplayer: the `.sbpmirror` file only exists on the PC that saved the blueprint. Blueprints downloaded from the host may preview unmirrored on clients.

Afterwards, or after a crash, keep `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`.

Not covered: multiplayer, dedicated servers, removing the mod for buildings other than the Refinery.
