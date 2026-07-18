# Star Trek: Elite Force Xbox TODO

Logged 2026-06-19 for tracking only. Do not treat these as immediate work items unless the project manager promotes them.

## Single-Player Playtest Bugs - 2026-07-17
User-observed during single-player play. These are logged for follow-up triage; not all items have code/log attribution yet.

### Intro And Loading
- Intro videos do not play; the game jumps straight to the main menu.
- Long stall after starting a new game before the loading screen appears; once the loading screen appears, it is only visible briefly.

### Menus
- Left stick should also control menus; verify this everywhere.
- Pause menu does not accept left-stick controls. Evidence: `borg2` direct load opened the pause menu correctly, but left-stick navigation failed there.
- Change `Y == Back` prompts/behavior to `B == Back`.
- Audio volume pips should be full height and centered over the current setting.
- Video safe-zone settings currently do nothing.
- Controller menu needs additional layouts; take layout ideas/assets from Unreal Tournament extras.
- Voyager Crew is not plumbed yet.
- Pause menu is very incomplete.
- Loading through the main menu can leave broken/stale menu/loading presentation over gameplay. Hypothesis to verify: some UI/loading state is not being flushed between the menu path and level entry.
- Proposed main menu update: add `COOPERATIVE` as a main-menu item between `LOAD GAME` and `HOLOMATCH`, and remove `CREDITS` from the main menu. Credits should remain accessible only by finishing the game. Preserve 1:1 LCARS layout quality when implementing.

### Gameplay
- First-level intro text crawl needs higher-resolution textures.
- Many voice lines do not play. This seems related to visible characters, because mouths do not move either.
- First-level intro text crawl slides can be seen through the wall during gameplay.
- First-level Borg have black sections.
- Player cannot shoot or perform many other expected actions.
- Some Starfleet NPCs show up as male Munro on `borg1`.
- Loading into `borg2` from `borg1` froze after the player spawned.

### Evidence - 2026-07-17
- `C:/Users/smmel/AppData/Local/Temp/codex-clipboard-5db936b2-44c1-4c33-9ea3-31ea10466d39.png` - direct `borg2` load: pause menu opens, but left-stick navigation does not work.
- `C:/Users/smmel/AppData/Local/Temp/codex-clipboard-a3ed901f-a86a-473b-bcf3-6b23d1ed5ede.png` - loading through main menu: broken/stale menu/loading overlay, suggesting state cleanup may be missing.
- `C:/Users/smmel/AppData/Local/Temp/codex-clipboard-782f2b26-27af-4c00-8ee6-cded1d4e9402.png` - proposed main menu update mockup showing `COOPERATIVE` added to the main list.

## Presentation And UI
- Boot loading wheel is simpler and more broken than the normal loading wheel; it should match the normal loading wheel 1:1.
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
- Co-op split-screen camera parity remains unfinished: P2 must match P1's first-person and third-person relative camera behavior, and P2 visibility from P1's view still needs sign-off.
- Multiplayer deathmatch and CTF are stretch goals.

## Expansion Content
- Implement the Voyager virtual tour expansion pack.
