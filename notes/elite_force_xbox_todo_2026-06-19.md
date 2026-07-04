# Star Trek: Elite Force Xbox TODO

Logged 2026-06-19 for tracking only. Do not treat these as immediate work items unless the project manager promotes them.

## Presentation And UI
- Restore animated faces when characters talk.
- Move loading presentation earlier than the cgame loading phase if possible. Current EF `CG_DrawInformation` path is correctly wired once `CA_LOADING`/cgame exist, but black screen can remain during pre-cgame transition work.
- Add a real pause menu for the Start button path. `cg_paused` is currently mapped but does nothing useful. The pause experience should include typical `RESUME`, `SAVE`, `LOAD`, and `OPTIONS` elements, display `PAUSED` somewhere on screen, and be perfectly LCARS themed.
- Investigate pause-menu item actions. `SAVE`, `LOAD`, `CONFIGURE`, `QUIT`, `EXIT`, and related entries appear to still be wired into Jedi Academy menus/flows instead of Elite Force behavior.
- Begin wiring in the actual game intro and main menu.

## Character And Cinematic Issues
- Fix Munro's cinematic model swapping torso LODs throughout the intro cutscene.

## Memory And Level Scale
- Several tested levels heavier than `borg1` load, but other systems such as characters fail, presumably due to out-of-memory conditions.
- Investigate map conversion to lumps and other optimization paths if needed.

## Packaging And Assets
- Go back to PK3-only packaging: keep the original PK3s as the base data, then layer `xbox[number].pk3` files as Xbox patch archives.

## Future Modes
- Once the game is running well in all aspects, pivot to co-op.
- Co-op P1 should take either Munro (male) or Alexa (female).
- Co-op P2 should get the other character, or possibly custom characters designed by the project manager.
- Co-op P2 should be treated as a pawn: no cutscene visibility, and Hazard Team AI should not follow them.
- Enemy AI should still attack P2.
- Multiplayer deathmatch and CTF are stretch goals.

## Expansion Content
- Implement the Voyager virtual tour expansion pack.
