# Holomatch Plan - 2026-07-15

## Source Truth
- Official Elite Force Holomatch v1.2 source is mirrored locally at `third_party_private\elite-force-holomatch-source-1.2\stvoy\Code-DM`.
- Public reference locations checked:
  - `https://www.moddb.com/games/star-trek-voyager-elite-force/downloads/star-trek-elite-force-holomatch-source-code-v12`
  - `https://www.frag-net.com/game_stef.html`
  - `https://code.idtech.space/raven/stvoy-mp-sdk`
- EF Holomatch source modules are `game`, `cgame`, and `ui`. Treat inherited Jedi Academy MP code as platform/toolchain scaffolding where EF behavior conflicts.
- Elite Force behavior should come from the official EF source. Inherited JA-only player model, animation, and attachment paths in this repo are bypassed for Holomatch rather than treated as missing EF behavior.
- UI behavior is mandated to come from the shared EF/SP UI path. The MP UI library may keep only the syscall/ABI adapter needed to host that code; inherited MP parser menus and `.menu` scripts are inactive for Holomatch.

## Current Match-Run Target
- Direct-boot MP Release into a small FFA on `hm_borg1`.
- MP Holomatch builds as `codemp\x_exe\Release\efmp.xbe`; the SP/co-op `default.xbe` artifact remains separate and must not be overwritten by Holomatch builds.
- Startup command:
  - `+set fs_game BaseEF +set model munro/default +set sv_maxclients 4 +set g_gametype 0 +set fraglimit 10 +set timelimit 0 +set g_ghostRespawn 5 +set g_holoIntro 0 +set bot_enable 1 +set bot_minplayers 0`
  - `map hm_borg1` is queued from the main loop after early startup instead of being appended directly to `Com_Init`, so map-load failures are separated from engine boot failures in the log.
  - Borg bot injection is deferred from the main loop with `addbot 1_of_12 4 free 3000` and `addbot 2_of_3 4 free 5000`, so logs separate map startup from bot entry.
- First match milestone is a live FFA loop on `hm_borg1`: map loads, players spawn, EF pickups exist, phaser-style starting weapon works, and bots can enter.

## Implemented First Slice
- MP `BASEGAME` and filesystem OS paths now route to `BaseEF` when `STEFX_ELITE_FORCE_MP` is defined.
- MP Holomatch build output is separated as `efmp.xbe` so it cannot overwrite the SP/co-op `default.xbe` artifact.
- MP XBE metadata now names the artifact `Star Trek: Elite Force Holomatch` while preserving the current test ID/LAN key.
- MP build isolation was re-audited in `scripts\build_xbox.ps1` and `codemp\x_exe\x_exe.vcproj`: the MP link output is `codemp\x_exe\Release\efmp.exe`, post-processing derives `codemp\x_exe\Release\efmp.xbe`, and only the SP/co-op path writes root `build\release\default.xbe`.
- MP `x_exe` NMake hooks now call `scripts\build_xbox.ps1 -Target mp`, so Visual Studio build/rebuild follows the same `efmp.exe` -> `efmp.xbe` path as the documented PowerShell build instead of an absent batch wrapper.
- If the MP GOB archive is absent in the loose-file test tree, Holomatch builds log that condition and fall back to loose-file reads.
- Holomatch loose-file reads and access/existence probes bypass the Xbox file-code cache precheck, and the file-code cache helpers now fail closed instead of dereferencing a missing cache or failed mutex after a nonfatal cache init failure.
- MP direct boot bypasses the frontend Holomatch/co-op menu mismatch and starts `hm_borg1`.
- MP direct boot now queues Borg bot entry after the first few main-loop frames instead of appending bot commands directly after `+map`, giving the smoke log a clear map-load checkpoint before bot injection.
- MP direct boot now waits until the server reports `hm_borg1` running before it queues Borg bot entry, logging either the running-map checkpoint or the wait condition so a failed map startup is not confused with an addbot failure.
- MP direct boot now also waits until the local client reaches active play before it queues Borg bot entry, so a local-player attach failure is not hidden behind successful bot startup.
- The direct local join path is now instrumented from map loading through server connect acceptance, so the first smoke log can distinguish map-load failure, localhost connect failure, and active client spawn failure.
- MP `ui_ef_holomatch` now routes through the SP/EF UI bridge to `map hm_borg1` in `efmp.xbe`; the split-screen baseline helper remains intact for the future split-screen project.
- `scripts\check_mp_holomatch_ui.py` now enforces the UI mandate during MP packaging and CXBX-R staging: `x_ui` must build only the SP/EF bridge plus shared EF UI modules, cgame must use the EF UI shim instead of the old menu renderer, and `xbox1.pk3`/staged `BaseEF` must contain zero UI `.menu`/`.txt` parser scripts.
- Latest MP rebuild (`build_mp_ui_mandate.log`) restaged `efmp.xbe` and `BaseEF\xbox1.pk3` with the mandate check passing for both repo output and the CXBX-R folder; `default.xbe` was not touched.
- Latest MP rebuild (`build_mp_shader_manifest_stage_cleanup.log`) rebuilt and restaged `efmp.xbe` plus `BaseEF\xbox1.pk3`; the package check passes with 22 shader scripts, zero UI parser scripts, DDS-only textures, BGRA32 alpha textures, and 1040 Holomatch support files. `default.xbe` remains untouched.
- Latest MP rebuild (`build_mp_stage_reset_debug_scrub_list22.log`) rebuilt and restaged `efmp.xbe` plus `BaseEF\xbox1.pk3`; `efmp.xbe` timestamp is 2026-07-16 17:18:23, staged `xbox1.pk3` timestamp is 2026-07-16 17:20:10, and staged `default.xbe` still has its 2026-07-13 timestamp.
- Latest MP rebuild (`build_mp_sp_interface_hud.out.log`) rebuilt and restaged `efmp.xbe` plus `BaseEF\xbox1.pk3`; `efmp.xbe` timestamp is 2026-07-16 17:32:32, staged `xbox1.pk3` timestamp is 2026-07-16 17:34:46, and staged `default.xbe` still has its 2026-07-13 timestamp. The active Holomatch HUD path now reports `STEFX_HM: registered ... EF SP interface HUD shaders`, `STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path`, and `STEFX_HM: EF SP interface HUD draw active ...`; stale `hardcoded HUD` breadcrumbs and old JA menu strings are absent from the staged XBE.
- MP renderer now explicitly resets Xbox texture stage 1 and restores stage 0 for single-texture draws after multitexture/bump paths, mirroring the SP EF texture-stage contract and logging `STEFX_HM: renderer reset Xbox texture stages ...` for the next runtime checkpoint.
- Direct Holomatch boot now forces debug rendering cvars off before queuing `map hm_borg1`, logging `STEFX_HM: direct Holomatch render debug cvars forced off before map`; this prevents stale `developer`/`r_showtris`-style settings from masquerading as shader failures.
- `xbox1.pk3` now authors `scripts\_console_shader_list_` from the final normalized package shader entries, including the Xbox-specific shader file, so runtime shader enumeration cannot go stale or miss package-injected shader scripts.
- Build-release shader helper lists now include the package-injected Xbox shader entry too, so loose build output reports the same 22 shader names as `xbox1.pk3`.
- CXBX-R Holomatch staging now removes stale loose `scripts\*.shader`, `scripts\_console_shader_list_`, `maps\hm_borg1.bsp`, and `maps\hm_borg1.aas` before copying the fresh package, so `xbox1.pk3` is the active authority for shader and map data during this vertical slice.
- The Xbox map-load cleanup/relocation path has been audited after `CL_MapLoading`: it moves the active client frame data but leaves the pending connection block and loopback packet queue intact, while `CL_ShutdownAll` unloads cgame/UI/renderer without clearing the queued localhost connect state.
- Holomatch server cvar defaults now match official EF for the first-match physics baseline: `g_knockback` defaults to `500`, while the stale inherited respawn cvar defaults to `0` in EF MP builds.
- The official EF `g_holoIntro` cvar is now registered for Holomatch builds with its EF default, while direct `hm_borg1` smoke explicitly sets it to `0` so the first match does not depend on the unported intro presentation.
- Raw EF `IBSP` v46 maps are loaded by collision and renderer through the shared Xbox BSP adapter.
- `hm_borg1.bsp` has been sanity-checked against that adapter: raw version is `IBSP` 46, lump ranges are valid, face/index packing fits the Xbox packed limits, and no surface/index violations were found.
- Normal Holomatch worldspawn now follows official EF map config behavior and returns before inherited player/saber asset preloads can run against BaseEF.
- Server spawn finalization now skips inherited ambient soundset precache in Holomatch builds; EF target speakers stay on their official direct sound path.
- Logs use `STEFX_HM:` markers across startup, filesystem, collision BSP load, renderer BSP load, and game entity spawning.
- The `hm_borg1` entity lump has been audited: all structural classnames are in the MP spawn table and all EF pickups are covered by aliases.
- Holomatch spawn parsing accepts the shipped `hm_borg1` `trigger_hurt` `damage` key as an alias for the inherited `dmg` field, so lethal EF hurt volumes do not fall back to default damage.
- Normal Holomatch `trigger_hurt` touch damage now follows official EF behavior: EF electro sound, EF slow/silent/no-protection spawnflag handling, and no inherited falling/death side path.
- EF pickup classnames used by `hm_borg1` now spawn through shared item aliases:
  - weapons: phaser, compression rifle, I-MOD, scavenger, Tetryon disruptor, dreadnought/arc welder.
  - ammo: compression rifle, I-MOD, scavenger, Tetryon disruptor, dreadnought.
  - health/armor: hypo, small hypo, combat armor.
- Shared weapon lookup prefers those EF aliases in Holomatch builds, so registered first-person/pickup models use EF asset paths while the temporary backing behavior still uses existing MP weapon slots.
- Cgame weapon registration now binds the hm_borg1 Holomatch weapons to EF sound media and skips the JA effect-registration branch for those temporary backing slots.
- Cgame weapon registration now allow-lists the six `hm_borg1` Holomatch bridge slots before generic model registration, so stale inherited weapon slots cannot load JA weapon media.
- Cgame sound registration now maps inherited footstep slots to EF `step`, `clank`, and `splash` media in Holomatch builds instead of missing JA surface-footstep names.
- Cgame teleporter/chat media now uses official EF paths (`sound/world/transin.wav`, `sound/world/transout.wav`, `sound/interface/communicator.wav`) instead of missing JA player teleporter/talk sounds.
- `target_speaker` now follows official EF spawn/use behavior in normal Holomatch, including `.wav` defaulting, nonfatal missing-noise handling, and no inherited sound-set/behavior side path.
- Cgame teleporter events now keep EF translocator sounds but skip the inherited JA spawn effect handle that is not preloaded for Holomatch; server teleports log the first source/destination pair.
- `trigger_multiple` now uses official EF timing and activation semantics in normal Holomatch, so `hm_borg1` teleporter triggers get the EF 0.5 second default wait and bypass inherited JA trigger side conditions.
- Server teleports now use EF's destination-clearance trace in normal Holomatch, nudging the player up only when the destination has room.
- Cgame and server fall/landing events now use EF `land1.wav` and `footsteps/metalland.wav` in Holomatch builds instead of missing JA `fallsplat` or `objectHit` media.
- Cgame utility media now uses BaseEF-safe handles in Holomatch builds: EF win/loss music, EF explosion model/shaders, EF medkit/zoom/water sounds, EF white/disrupt shaders, and null handles for unused force/seeker/holocron sounds.
- Cgame effect shader preload now keeps the BaseEF-safe rivet mark and skips inherited JA saber/force shader registration in Holomatch builds.
- Cgame graphics startup now skips the inherited JA effect bundle that points at missing saber/force/turret/vehicle effect files, leaving those handles deliberately empty for the Holomatch smoke test.
- Cgame graphics startup now skips the inherited JA weapon model cache in Holomatch builds, avoiding weapon-instance setup for a model system EF does not use.
- Cgame graphics startup now maps active FFA HUD/chat, item respawn, shield/pain overlay, wall-mark, and chunk media to BaseEF-safe assets, and zeros inherited JA force/radar/powerup shell handles that are not part of the `hm_borg1` smoke path.
- Cgame now compiles out the unused inherited debris helper in Holomatch builds; live breakable/debris events use the EF-safe chunk media registered for `CG_Chunks`.
- Cgame item rendering now skips the inherited JA item projection model/effect in Holomatch builds so EF pickups render through their registered pickup/world models without asking for missing JA item media.
- Cgame item rendering now treats invalid EF item entity indices as nonfatal logged skips during the smoke test, so one stale item snapshot cannot drop the match.
- Cgame item visual registration now allow-lists the EF `hm_borg1` pickup surface before model registration, so stale inherited item bits cannot load JA item/weapon models.
- Cgame startup now registers official EF announcer/countdown/prepare/frag media and skips inherited JA saber/force bulk sound preloads in Holomatch builds.
- Cgame startup now registers only the six EF-backed `hm_borg1` weapon icons, reuses those handles for unavailable states, and skips inherited inventory icon preloads for normal Holomatch.
- Cgame startup now skips inherited team-order/statusbar cursor, flag, half-shield, and Force-touch media in normal Holomatch, preventing another batch of BaseEF-missing UI/effect lookups after model and weapon init.
- Cgame spectator-list/class-count code now no-ops inherited JA class media in normal Holomatch, and stale class/Force server commands are ignored before they can open inherited menus or parse inherited class data.
- Cgame startup and live configstring handling now ignore inherited JA mode state/objective/timer updates in normal Holomatch and clear local timer/win-team state if such data leaks.
- Holomatch helper predicates in cgame server-command handling, entity rendering, console commands, player death, and damage now resolve directly to the EF build path instead of defining Holomatch as "not an inherited JA game mode."
- Cgame obituary handling now uses EF-style Holomatch text (`Casualty`, `Credit`, `Method`) and maps the temporary backing weapon mods to EF methods such as phaser burns, energy scars, infinite modulation, perforated, welded, and degaussed instead of looking up inherited JA obituary strings.
- Cgame obituary handling now treats invalid Holomatch death-event targets as nonfatal logged skips during the smoke test, so a stale death event cannot drop the client.
- Cgame no-ammo feedback now registers EF `sound/weapons/noammo.wav` for Holomatch builds.
- Shared player-state-to-entity-state copies now clear inherited JA model-instance animation/model flags plus stale Force/saber/vehicle/effect snapshot state in Holomatch builds.
- First cgame snapshots now skip the inherited JA player model duplication path and clear any stale player model/weapon attachment pointers before prediction/render setup.
- Cgame prediction now skips inherited player model collision setup in Holomatch builds and clears stale local player model pointers before movement prediction.
- Cgame weapon instance helpers now no-op in Holomatch builds, so stray JA callers cannot copy weapon models into a non-existent player model instance.
- Cgame missile impact handling now short-circuits Holomatch-backed temporary weapon slots before inherited JA impact FX, playing any registered EF hit sound/light instead of calling unregistered JA effect handles.
- Server traced EF weapon fire now emits BaseEF-safe impact events for the temporary Phaser, Compression Rifle, I-MOD, Tetryon, and Dreadnought bridge paths, so `hm_borg1` smoke tests can verify shot impacts without unported effect bundles.
- Cgame Tetryon disruptor bridging now catches the inherited disruptor event family and turns it into EF-safe Tetryon fire, hit, and zoom feedback instead of calling JA disruptor effects or sound paths.
- Cgame vehicle-impact override checks now return dormant in Holomatch builds, because the JA vehicle effect table is intentionally skipped for the `hm_borg1` smoke path.
- Cgame generic effect handling now short-circuits the bridged I-MOD alt detonation before the inherited JA DEMP2 visual effect path, playing registered EF sound/light feedback instead.
- Cgame event handling now drops inherited predefined Force sound events in normal Holomatch before they can register old Force media.
- Cgame event handling now skips inherited NPC voice and stale spectator-mode events before temporary NPC client allocation or custom-sound lookup, so stale events cannot wake the old `sound/chars` voice path during `hm_borg1`.
- Cgame event handling now skips inherited model-mark, duel, free-saber, and holdable-use events in normal Holomatch before they can run old model, duel, or inventory handlers.
- Cgame event handling now treats unknown Holomatch events as nonfatal one-time logged skips during the smoke test, preserving the entity/event payload for follow-up instead of dropping the client.
- Cgame sound events now bounds-check Holomatch sound indices before touching sound/configstring arrays, logging one bad payload instead of risking an out-of-range read.
- Cgame effect events now bounds-check Holomatch effect indices before touching effect/configstring arrays, falling back to normal impact handling when custom effect payloads are bad or empty.
- Cgame sound-set, client-index, and entity-index event paths now bounds-check Holomatch payloads before indexing ambient-set configstrings, client arrays, or entity arrays.
- Cgame weapon registration now registers the EF `_flash.md3` view-model flash and draws it on Holomatch fire events, so first-person smoke tests get visible muzzle feedback from BaseEF weapon media instead of relying only on old effect handles.
- Cgame taunt events now use the EF-style loaded `taunt1` through `taunt5` client sounds in Holomatch builds instead of the inherited duel/anger/deflect/gloat fallback tree.
- EF weapon/pickup model, icon, and sound paths used by the temporary hm_borg1 bridge have been checked against `build\release\BaseEF`.
- First-person EF weapon hands use a static frame fallback in Holomatch builds, avoiding JA torso-animation frame numbers on EF's simpler hand models during the smoke test.
- Cgame now draws a BaseEF-safe EF beam fallback for Phaser and Dreadnought primary fire, so continuous beam weapons have visible smoke-test feedback without depending on unported full EF effect bundles.
- FFA spawn loadout is forced to a phaser-backed base weapon with phaser ammo.
- Client spawn now seeds valid standing/ready animation fields in the EF path after skipping inherited setup, giving the first live frames deterministic state for the smoke test.
- Normal Holomatch player spawn selection now follows official EF FFA behavior: random non-telefrag deathmatch spawn, retrying away from the nearest death location instead of the inherited furthest-spawn selector.
- Holomatch shared player bounds now use the official EF player shape (`mins -15 -15 -24`, `maxs 15 15 32`, stand viewheight `26`, crouch viewheight `12`) instead of the inherited JA taller box.
- Holomatch shield protection now uses EF's full normal-damage shield absorption constant rather than the inherited half-absorb constant.
- Bot out-of-ammo and inherited melee-fallback selection now use the same phaser bridge slot in Holomatch builds instead of hard-coded JA weapon slot `1`.
- Bot ideal/choice weapon checks now keep the EF Phaser bridge selectable while its rechargeable bucket is empty, matching the player selector and avoiding bot fallback churn during recharge.
- Bot combat heuristics now treat the EF Phaser bridge as a normal Holomatch weapon, disable inherited projectile aim-leading for it, and compile out the close-range inherited saber preference in Holomatch builds.
- Bot combat now lets an empty EF Phaser bridge stay in the normal combat/selection path instead of entering the inherited out-of-ammo fallback gate during recharge.
- Bot combat now skips inherited Force-power decision and activation paths for normal Holomatch matches, preventing stale Force state from steering Borg bot behavior on `hm_borg1`.
- Bot combat now skips inherited saber/duel combat branches for normal Holomatch matches, while keeping ranged aiming, navigation, and EF-backed weapon selection active.
- Bot frames now correct any stale inherited melee/saber weapon state back to the EF Phaser bridge before range, aiming, or attack decisions run.
- Bot setup now seeds EF-safe weapon defaults before personality parsing and compiles old bot team/objective branches out of Holomatch builds, so missing personality data or stale mode values cannot steer the `hm_borg1` bots toward inactive inherited weapon/team behavior.
- Cgame `weapon` commands now use EF Holomatch numbering for the bridged hm_borg1 weapon slots, while Xbox hotswap's `weaponclean` command remains an internal bridge-slot selector.
- Server weapon pickups and client pickup prediction now use the bridged weapon's ammo bucket instead of the weapon enum when granting or seeding ammo.
- Cgame Holomatch weapon-pickup prediction now mirrors the server's bridged ammo bucket quantity/clamp behavior instead of only seeding one predicted ammo unit.
- Server pickup events now match official EF Holomatch by sending the item index directly during normal Holomatch, while inherited event-entity payloads remain reserved for non-Holomatch paths.
- Cgame pickup events now read the direct EF item index in normal Holomatch and skip the inherited entity-table pickup debounce in that path.
- Cgame local pickup feedback now uses an EF-shaped display/autoswitch path in normal Holomatch: the pickup HUD state is kept, old pickup string-table prints are skipped, and autoswitch safe mode treats the six EF-backed `hm_borg1` weapons as selectable safe weapons.
- Holomatch no-ammo feedback now distinguishes failed primary fire from failed alt fire before cgame fallback weapon selection, matching official EF's alt-select behavior instead of using the inherited one-size event path.
- Normal Holomatch score commands now use the official EF 11-field per-player payload on both server and cgame, including worst enemy, favorite weapon, killed count, and eliminated state, instead of the inherited JA accuracy/award/capture payload.
- Normal Holomatch now initializes EF-style weapon stat tracking and records weapon pickups, weapon fire, nonfatal damage, kills, deaths, and frags into the score-stat helpers, with bounds guards around the inherited enum/table bridge.
- Normal Holomatch now clears those EF-style score/stat counters on client begin and disconnect, matching the official EF client lifecycle instead of keeping the old commented-out cleanup hooks.
- Normal Holomatch exit rules now follow official EF ordering for warmup, timelimit sudden death, `timelimitWinningTeam`, fraglimit, and capturelimit, bypassing the inherited early tied-score return that could skip limit checks outside the actual timelimit path.
- Cgame keeps the EF Phaser bridge selectable even when its rechargeable phaser bucket is empty, so the empty-fire/recharge path is reachable instead of immediately falling through inherited no-ammo autoswitch rules.
- Empty Phaser movement handling now lets the helper own the fire delay; idle empty Phaser suppresses autoswitch without adding delay, while an actual empty-fire event gets exactly one Phaser fire interval.
- EF combat armor now uses the official item tag/value data and follows the official Holomatch 200-armor ceiling in both shared pickup prediction and server pickup clamping; the first armor pickup logs before/after armor values.
- Holomatch game init now defaults and forces the inherited JA projectile model-trace cvar off, so EF-backed weapons use normal trace/missile collision during the `hm_borg1` smoke test.
- Server player-model setup bypasses inherited JA skeletal model-instance creation in Holomatch builds, including the old `kyle/model.glm` path.
- Cgame player loading registers `models/players2/<model>/head.md3` as a visible marker when full EF MDR body rendering is not available yet.
- Cgame player loading discards inherited JA player/entity model pointers so the temporary Holomatch marker path is always used.
- Cgame client sounds now use EF Holomatch sound names, read `sex`/`soundpath` from `models/players2/<model>/animation.cfg`, prefer `sound/voice/<soundpath>/misc`, and fall back to `sound/player/hm_male` or `sound/player/hm_female` instead of JA `sound/chars` paths.
- Cgame NPC custom sound precache/config hooks now stay dormant in normal Holomatch and clear any stale NPC custom sound handles, so leaked inherited sound configstrings cannot probe missing `sound/chars` directories during the `hm_borg1` smoke path.
- Cgame client-info load/copy/deferred paths now preserve marker models, skins, icons, and sounds without attaching or duplicating inherited JA model instances.
- Cgame client-info loading now compiles old client placeholder/sound-skip/team-skin branches out of Holomatch builds, so `munro`, `1_of_12`, and `2_of_3` always stay on the EF marker and `players2` sound path during the `hm_borg1` smoke test.
- Cgame Holomatch marker registration now has an explicit `munro` fallback for missing/invalid model strings, independent of inherited default-model values.
- Server Holomatch client-info now defaults empty userinfo models to `munro/default`, serializes a compact EF-only player config string, and bypasses inherited server-side model-instance refresh during client-info changes.
- Client connect now self-corrects stale spectator session data back to active free play for normal Holomatch FFA when the userinfo did not explicitly request spectator, and logs the final team assignment.
- Client begin now logs a compact active-state summary after spawn, including bot flag, session team, persisted team, current weapon, health, armor, and remaining spawn-protection milliseconds, so the `hm_borg1` smoke can prove clients entered active free play.
- Cgame Holomatch client-info reuse now ignores inherited saber/model-instance match requirements, clears stale inherited weapon model pointers when marker handles are copied, and skips the Xbox inherited player-model memory free during model changes.
- Holomatch client/server startup and frame processing now skip JA vehicle parms, JA saber parms, the JA jetpack model cache, and inherited per-frame Force/saber updates for normal Holomatch players, all with `STEFX_HM:` log markers.
- EF cgame startup now skips inherited JA mode init/class counting and inherited startup model config probes unconditionally; those JA-only paths should remain inert in Holomatch.
- Holomatch client frame processing now scrubs stale inherited Force/saber/vehicle/jetpack/cloak player-state fields before old vehicle input, stance, movement, and snapshot logic can read them.
- Holomatch client frame processing now skips inherited saber stance selection, so stale saber config cannot repopulate Force/saber animation fields after the frame scrub.
- Holomatch client frame processing now clears broadcast visibility bits and skips inherited Force/Jedi visibility broadcasts, so stray sight/master state cannot make normal FFA clients network-visible through old rules.
- Holomatch client frame processing now no-ops inherited generic command IDs in normal FFA, preventing stray old Force/saber/holdable commands from reaching runtime handlers while leaving firing and weapon selection on their bridged paths.
- Holomatch server frame, pickup, trigger, target speaker, teleporter, score, weapon-stat, and map-restart feedback paths now take the EF branch directly instead of choosing EF behavior through inherited JA gametype comparisons.
- Cgame command registration now follows the EF Holomatch command surface for normal FFA: old Force/saber/holdable forwarding commands are not registered, EF-style `+zoom`/`-zoom` commands are available, and any unexpected inherited selector command is ignored before it can mutate client prediction state.
- Cgame startup now skips inherited Force icon preloads and maps generic damage mark handles to EF-safe damage shaders, avoiding BaseEF lookups for old UI and saber-glow assets during `CG_Init`.
- Holomatch client begin/spawn and cgame client-info parsing now skip JA saber entity creation, bot saber setup, spawn saber sync, and cgame saber model setup. This keeps BaseEF runs away from missing JA saber models like `models/weapons2/saber_reborn/saber_w.glm`.
- EF userinfo and spawn paths now compile out inherited saber sync, saber animation repair, Force/saber loadout repair, class weapon/inventory/powerup setup, and force initialization branches; the active Holomatch path logs skips before applying the EF Phaser loadout.
- Server frame saber traces/missile-block updates are gated behind actual saber state in Holomatch builds so normal EF-backed players do not run per-frame JA saber code.
- Cgame HUD menu loading now returns safely if both the requested HUD and fallback HUD are missing, instead of reading through an invalid file handle.
- Early cgame loading art now registers known BaseEF loading textures instead of JA frontend saber/loading shaders.
- Cgame loading information panels now use EF map/unknown-map levelshots and skip inherited JA gametype levelshots such as `levelshots/mp_ffa`.
- Server weapon fire now logs one begin/end pair for each Holomatch-backed weapon slot, giving the first trigger pull a clear crash boundary.
- Server weapon fire for the `hm_borg1` weapon set now bypasses inherited JA fire routines in Holomatch builds and uses EF-style server behavior behind the temporary backing slots:
  - phaser: trace beam with EF point-blank falloff.
  - compression rifle: EF-style trace damage, spread, and impact splash.
  - I-MOD: EF-style piercing trace damage.
  - scavenger: EF-style projectile and arcing alt projectile.
  - Tetryon disruptor: EF-style multi-trace primary and projectile alt.
  - dreadnought/arc welder: EF-style paired trace primary and projectile alt.
- Shared Holomatch weapon data now uses EF-style ammo buckets, costs, and fire times for the `hm_borg1` bridge slots:
  - phaser uses the old phaser/force ammo bucket with a 50-ammo ceiling and a 100 ms recharge tick.
  - compression uses primary 1 / alt 8 ammo and 250 / 1200 ms fire times.
  - I-MOD uses primary 1 / alt 3 ammo and 350 / 700 ms fire times.
  - scavenger uses primary 1 / alt 5 ammo and 100 / 700 ms fire times.
  - Tetryon disruptor uses primary 1 / alt 2 ammo and 100 / 200 ms fire times.
  - dreadnought/arc welder uses primary 1 / alt 5 ammo and 100 / 500 ms fire times.
- Shared movement now treats the six bridged Holomatch weapons as non-JA charge weapons, so Scavenger, I-MOD, and Dreadnought alt-fire events reach the EF bridge immediately instead of entering inherited charge/lock states.
- Shared movement now gates inherited disruptor zoom/lock behavior away from the Holomatch Tetryon bridge, so Tetryon primary and alt-fire are normal EF weapon events rather than JA zoom control.
- Shared movement now emits an EF empty-Phaser event when the Phaser bucket is empty or the last primary charge is consumed, and the server Phaser bridge checks the Phaser bucket rather than the old blaster bucket when applying empty-shot damage behavior.
- Shared movement now clears inherited JA rocket-lock state in Holomatch builds, server fire clears any stale lock state before bridged Holomatch shots, and cgame skips the matching lock HUD branch if stale snapshot state appears.
- Holomatch shared entity types now follow the official EF core ordering through `ET_USEABLE` in EF MP builds, with inherited-only entity types kept after that and the botlib mirror updated through the mover slot so AAS entity classification remains aligned.
- The Xbox botlib project and its file-level compiler override now define the EF MP flag too, so its local entity-type mirror is compiled with the same Holomatch ordering as game and cgame.
- Cgame's active HUD now skips inherited JA Force, inventory, jetpack, cloak selector, and clash-flare overlays in Holomatch builds while leaving the weapon selector path active.
- EF hardcoded Holomatch HUD interface pieces now draw DDS textures with native `0..1` UVs instead of forcing a V flip; the previous forced flip matched the upside-down HUD symptom seen in CXBX-R.
- Client spawn now clears preserved inherited Force/saber state before Holomatch loadout decisions, skips inherited Force/saber animation/loadout branches, and skips the per-spawn Force initializer, so old state cannot steer the EF loadout path before the Phaser bridge is applied.
- Holomatch respawn now uses a direct EF respawn path and returns before inherited game-mode respawn branches.
- Holomatch spawn protection now uses EF's `g_ghostRespawn` cvar (default 5 seconds) bridged onto the existing spawn-protection flag at the same point in the spawn flow as official EF, before spawn targets and the first client frame. It blocks all non-telefrag damage while active, including old no-protection environmental damage, and item pickup clears it the way official EF clears `PW_GHOST`.
- Holomatch fire events now log when firing clears EF ghost respawn protection, matching official EF's unghost-on-fire behavior.
- Server missile deflect visuals now skip inherited JA effect names for Holomatch-backed projectile weapons, and cgame ignores any inherited JA saber-family event that still leaks into snapshots.
- Server missile impact damage now skips the inherited JA half-absorb rule for Holomatch-backed projectile weapons, so EF Scavenger and Dreadnought projectile impacts use normal EF-style damage handling.
- Server missile impact events now clear the inherited player-surface mark flag for Holomatch-backed projectile weapons before the event reaches cgame.
- Server missile think now removes stale inherited saber/model-part projectiles in Holomatch builds before bounce, impact, or body-part logic can run.
- Server frame and missile-impact continuation paths now treat EF alternate missile entities as missiles in Holomatch builds, matching official EF projectile semantics.
- Server radius damage now skips inherited explosion player-mark temp events for normal Holomatch damage.
- Cgame split-screen render filtering, client prediction self-missile checks, and server hit accounting now treat EF alternate missile entities as projectiles in Holomatch builds.
- Server diagnostics now count and print EF alternate missile entities as projectiles, so first-run logs and entity dumps do not hide live Holomatch alt projectiles.
- Server/cgame diagnostics and render dispatch now recognize EF `ET_USEABLE` as a valid inert Holomatch entity type.
- Cgame Holomatch weapon rendering now uses an approximate muzzle origin for marker-only third-person players instead of looking for a JA weapon bolt on a non-existent model instance.
- Cgame missile entities for Holomatch-backed weapon slots now render EF-safe light/loop-sound feedback and return before inherited JA projectile model/trail paths.
- Cgame render dispatch now treats explicit official EF alternate missile entities as Holomatch projectiles when they carry one of the bridged `hm_borg1` weapons, while keeping a stale-shape compatibility path for old snapshots.
- Cgame missile rendering now skips stale inherited saber/model-part projectiles in Holomatch builds before they can register old weapon models.
- Cgame generic entity rendering now scrubs stale inherited JA model state in Holomatch builds before it can initialize a JA-style model instance.
- Cgame generic entity rendering now skips inherited bolted-object, body fade/disintegration, holocron, flying-saber, and tripmine/Force-sight visual branches in normal Holomatch; the official EF general-entity path does not carry those branches.
- Player death/body paths now use an EF-style three-animation death cycle and skip inherited JA corpse queue, server death-animation picking, dismemberment, attachment cleanup, stale cgame corpse-restore commands, model cleanup events, and per-frame model kill queues. EF does not have that inherited JA player model path, so these are explicit JA bypasses rather than missing-body fallbacks.
- Holomatch server entity-free and cgame server-command paths now drop inherited model cleanup queue/command traffic entirely; stale model flags or pointers are cleared in place without calling the inherited model cleanup API.
- Normal Holomatch player death now clears stale inherited saber/Force state and skips inherited jetpack/detpack death cleanup, while death attribution guards stale/null other-killer state before reassigning environmental kills.
- Holomatch now skips inherited limb-break and ragdoll callback paths, preventing old bodyfall/pain sound probes if stale body-damage logic is reached.
- Server combat damage entry now scrubs stale inherited Force/saber/duel/vehicle/jetpack/cloak state from target, attacker, and inflictor before damage rules run, so a hit that lands before the next client frame still sees EF-clean player state.
- Server combat damage now skips inherited DEMP2 shock/electrify/weapon-stall and one-third-damage behavior for the I-MOD bridge in Holomatch, keeping the temporary backing slot aligned with official EF I-MOD damage behavior.
- Server first-time Force initialization now clears stale Force state and returns in normal Holomatch before inherited free-saber, rank-menu, and profile-menu broadcasts can be emitted.
- Client spawn now matches official EF Holomatch by forcing the base Phaser bridge weapon directly into the ready state after the player is linked, skipping the inherited weapon-raise animation/state for Holomatch respawns.
- Cgame item visual registration now clears the full item visual structure before registering EF pickup media and skips inherited `.glm` model-instance setup in Holomatch builds.
- Cgame startup and live configstring model registration now ignore inherited `.glm`/special model strings in normal Holomatch before they can run the old model-instance or custom-skin cache path.
- Renderer model allocation now bypasses the inherited Xbox player-model slot allocator in Holomatch builds, so any stale skeletal player model that leaks through uses normal model allocation instead of JA-specific `models/players/.../model.glm` slot/refcount cleanup.
- MP renderer now loads and draws EF segmented `players2` body assets through the MDR path for Holomatch clients, registering lower/upper/head model and skin pieces and logging load/register/draw checkpoints.
- Cgame EF segmented player animation now remaps legs-only rows from the shared EF `animation.cfg` timeline into the lower body model's local frame range, while leaving full-body death rows and torso rows unchanged; the runtime log gate now fails if the renderer has to clamp player frames again.
- `xbox1.pk3` now carries Holomatch support data for `hm_borg1`, including AAS, bot metadata, all non-texture `botfiles/bots` data, `players2` model/skin/animation files, and DDS-only converted textures.
- The `hm_borg1` shader manifest's skipped texture candidates are shader-only names/editor images; their live stage images (`light3`, `light5`, `dreadnought/light5`, `engineering/glass1`, Borg beams/dust, etc.) are present as DDS entries in staged `xbox1.pk3`.
- Asset checks currently pass for `hm_borg1.bsp`, `hm_borg1.aas`, bot metadata, Borg bot personality files, EF segmented player body assets, EF pickup models/icons, EF pickup sounds, EF item respawn sounds, EF weapon sounds, and BaseEF loading art in `build\release\BaseEF`.
- Latest `hm_borg1` pickup media recheck confirms all exact EF alias model/sound paths exist in staged `BaseEF`, and the alias icon names resolve either as image assets or shader script entries.
- Direct boot now queues the named Borg bots from `hm_borg1`'s arena entry (`1_of_12` and `2_of_3`) after map startup instead of relying on random `bot_minplayers` selection.
- Bot init, command/add/connect/queue, and personality setup paths now have `STEFX_HM:` breadcrumbs so first-run logs show whether the bot library initialized, metadata loaded, named bots were accepted/rejected/queued, and EF weapon weights were mapped.
- Bot waypoint init now resets both the waypoint pointer table and waypoint count, avoiding stale count state that could make the Holomatch fallback graph skip itself on a fresh map.
- Holomatch builds load EF metadata from `scripts/bots.txt` and `scripts/arenas.txt`; the inherited JA defaults looked under `botfiles/bots.txt`, which EF does not ship.
- EF `scripts/bots.txt` entries use `aifile` instead of JA's `personality`; Holomatch addbot now maps `aifile` into the bot personality field so Borg bots load `botfiles/bots/1of729_c.c` and `botfiles/bots/2of3_c.c`.
- EF bot character files are not JA `.jkb` personality files; Holomatch builds now parse the official EF `skill` blocks directly, bridge the important aim/reaction/view/camper traits into the existing bot state, and map EF weapon-weight files onto the temporary backing weapon slots used by `hm_borg1`.
- EF bot weapon-weight lookup tolerates the shipped Borg filename case mismatch (`Borg_w.c` versus `borg_w.c`) and logs the exact mapped phaser/compression/I-MOD/scavenger/Tetryon/dreadnought weights.
- EF ships `maps/hm_borg1.aas`, but not JA `.wnt` botroutes. The current bridge logs the missing `.wnt` route data as a fallback-navigation condition, not a match-load failure.
- When JA `.wnt` route data is absent, Holomatch builds now create a temporary bot waypoint graph from player starts and visible item/pickup connector points, fill fallback segment distances, then log the generated starts, items, connectors, and links.
- `scripts\check_mp_holomatch_log.ps1` checks the post-run `ef_mp_log.txt` for map, local client, bot, HUD, score, and EF body-render checkpoints, and now exits stale if the selected log predates the staged `efmp.xbe`.

## Remaining Parity Gaps
- Current MP game/cgame are still inherited-engine shaped; UI behavior is SP/EF-only through the MP adapter:
  - Full EF weapon effects, ammo model, enum names, and cgame event parity are not ported yet; server damage/projectile behavior for the `hm_borg1` weapon set is now EF-style behind temporary backing slots.
  - EF per-weapon ammo storage is approximated through available shared ammo buckets for this smoke checkpoint; phaser has its own bucket, while I-MOD/Scavenger/Tetryon share the alien-energy bucket until the full EF player-state ammo model is ported.
  - EF segmented player bodies are now rendered through the MP MDR path, but runtime CXBX-R validation is still needed for animation attachment correctness and materials.
  - Full EF botlib item desirability and weapon-state behavior are not ported yet; the current bridge maps the official Borg weapon weights needed for `hm_borg1` onto the temporary backing slots.
- Next implementation pass should use CXBX-R logs and runtime behavior to chase remaining weapon/player-state parity, bot navigation/combat quality, and any body-render/material issues.

## First Test Log Markers
- `STEFX_HM: startup command +set fs_game BaseEF +set model munro/default ... +set bot_minplayers 0`
- `STEFX_HM: queueing direct Holomatch map frame=... sv=... map='...'`
- `STEFX_HM: CL_MapLoading begin cl_running=... state=... server='...'`
- `STEFX_HM: CL_MapLoading queued localhost challenge state=... addressType=...`
- `STEFX_HM: client sent local Holomatch connect packet attempt=... qport=... protocol=...`
- `STEFX_HM: CL_MapLoading after initial resend state=... connectPackets=...`
- `STEFX_HM: SV_SpawnServer game ready map='hm_borg1' state=... sv_running=1 maxclients=4`
- `STEFX_HM: server received local Holomatch connect protocol=... qport=... challenge=... name='...' model='...'`
- `STEFX_HM: server assigned Holomatch client slot=... local=1 reconnect=...`
- `STEFX_HM: GAME_CLIENT_CONNECT accepted client=... local=1`
- `STEFX_HM: server sent Holomatch connect response client=... state=...`
- `STEFX_HM: direct Holomatch map is running map='hm_borg1' frame=...`
- `STEFX_HM: direct Holomatch local client is active frame=... state=... mapFrames=...`
- `STEFX_HM: queueing direct Holomatch Borg bots frame=... mapFrames=... clientFrames=...`
- `JAMP: FS_Startup basepath=... gamedir='BaseEF'`
- `STEFX_HM: FS_Startup archive '...' missing; using loose-file fallback` (expected when testing from loose `BaseEF` files)
- `STEFX_HM: loose-file OS read path active, first='...'`
- `STEFX_HM: FS_FileExists Xbox check active, first='...'` (only appears if renderer/video startup asks for a file existence check)
- `STEFX_HM: filecode cache unavailable at ...; loose-file callers may continue` (only appears if the old Xbox file-code cache is missing/unusable)
- `STEFX_HM: CM_LoadMap raw probe loaded name='maps/hm_borg1.bsp'`
- `STEFX_HM: CM_LoadMap raw BSP complete`
- `STEFX_HM: RE_LoadWorldMap raw probe begin name='maps/hm_borg1.bsp'`
- `STEFX_HM: R_EFLoadRawWorldData complete`
- `STEFX_HM: G_InitGame map='hm_borg1'`
- `STEFX_HM: server defaults g_speed=250 g_gravity=800 g_knockback=500 g_weaponrespawn=5 g_adaptrespawn=1 g_ghostRespawn=5 g_holoIntro=0 legacyRespawn=0`
- `STEFX_HM: worldspawn used EF Holomatch config music='...' message='...' gravity='...'`
- `STEFX_HM: server skipped inherited soundset precache in Holomatch`
- `STEFX_HM: trigger_hurt spawn damage=... key='...' value='...' spawnflags=... origin='...'`
- `STEFX_HM: trigger_hurt used EF damage path damage=... spawnflags=... dflags=... sound=...` (first hurt brush touch)
- `STEFX_HM: spawned EF pickup classname='weapon_compressionrifle'`
- `STEFX_HM: loaded bot metadata file='scripts/bots.txt' count=...`
- `STEFX_HM: loaded arena metadata file='scripts/arenas.txt' count=...`
- `STEFX_HM: bot init sequence begin map='hm_borg1' restart=0`
- `STEFX_HM: BotAISetup trap_BotLibSetup begin`
- `STEFX_HM: BotAISetup trap_BotLibSetup done`
- `STEFX_HM: G_InitBots done bots=... arenas=... bot_minplayers=...`
- `STEFX_HM: inherited projectile model traces disabled for Holomatch`
- `STEFX_HM: skipping inherited vehicle parms in game`
- `STEFX_HM: skipping inherited saber parms in game`
- `STEFX_HM: skipping inherited per-frame Force/saber update in Holomatch`
- `STEFX_HM: skipping inherited vehicle parms in cgame`
- `STEFX_HM: skipping inherited jetpack model cache in cgame`
- `STEFX_HM: skipping inherited saber parms in cgame`
- `STEFX_HM: using BaseEF loading art handles`
- `STEFX_HM: cgame using EF loading levelshot media map='hm_borg1'`
- `STEFX_HM: cgame registered EF announcer media`
- `STEFX_HM: cgame used EF warmup prepare sound` (only appears if a warmup start configstring reaches cgame)
- `STEFX_HM: cgame skipped inherited saber/power shader preload`
- `STEFX_HM: cgame skipped inherited graphics effect preload`
- `STEFX_HM: cgame skipped inherited weapon model cache in Holomatch`
- `STEFX_HM: cgame skipped inherited weapon registration in Holomatch weapon=...` (only appears if a stale unsupported weapon slot is requested)
- `STEFX_HM: cgame skipped inherited Force icon preload in Holomatch`
- `STEFX_HM: cgame registered EF Holomatch weapon icons`
- `STEFX_HM: cgame skipped inherited inventory icon preload in Holomatch`
- `STEFX_HM: cgame skipped inherited item visual registration in Holomatch item=... classname='...'` (only appears if a stale unsupported item bit is requested)
- `STEFX_HM: cgame skipped EF item entity with bad index=... ent=...` (only appears if an invalid item snapshot reaches cgame)
- `STEFX_HM: cgame skipped inherited simple item sprite with no EF media item=... classname='...'` (only appears if a stale unsupported item entity reaches the simple-item render path)
- `STEFX_HM: cgame skipped inherited item render with no EF media item=... classname='...'` (only appears if a stale unsupported item entity reaches the model render path)
- `STEFX_HM: cgame registered EF damage mark media`
- `STEFX_HM: cgame skipped inherited team-order/statusbar media in Holomatch`
- `STEFX_HM: cgame skipped inherited mode init in Holomatch`
- `STEFX_HM: cgame skipped inherited mode config parse in Holomatch`
- `STEFX_HM: cgame skipped inherited class-count media in Holomatch`
- `STEFX_HM: cgame skipped inherited power/saber/holdable command registration in Holomatch`
- `STEFX_HM: cgame skipped inherited NPC sound precache in Holomatch '...'` (only appears if an old NPC sound configstring leaks)
- `STEFX_HM: cgame skipped inherited NPC custom soundset in Holomatch set=... dir='...'` (only appears if a stale NPC soundset load reaches cgame)
- `STEFX_HM: cgame ignored inherited NPC sound config in Holomatch ent=...` (only appears if an entity arrives with stale NPC sound config)
- `STEFX_HM: cgame skipped inherited startup model config in Holomatch index=... model='...'` (only appears if old model config leaks during media startup)
- `STEFX_HM: cgame ignored inherited model configstring in Holomatch index=... model='...'` (only appears if old model config changes at runtime)
- `STEFX_HM: cgame ignored inherited mode configstring in Holomatch index=...` (only appears if old mode config changes at runtime)
- `STEFX_HM: cgame ignored inherited mode/power server command '...' in Holomatch` (only appears if an old server command leaks)
- `STEFX_HM: cgame ignored EF obituary with invalid target=... attacker=... mod=...` (only appears if a stale/invalid death event reaches cgame)
- `STEFX_HM: cgame ignored inherited console command '...' in Holomatch` (only appears if an old command reaches cgame)
- `STEFX_HM: cgame ignored inherited selector command '...' in Holomatch` (only appears if an old selector function is called directly)
- `STEFX_HM: cgame registered EF chat/net HUD media`
- `STEFX_HM: cgame registered EF item respawn/shield media`
- `STEFX_HM: cgame registered EF chunk media`
- `STEFX_HM: cgame registered EF overlay/mark media`
- `STEFX_HM: cgame skipped inherited item projection media`
- `STEFX_HM: cgame registered EF no-ammo media`
- `STEFX_HM: cgame registered EF teleporter/chat media`
- `STEFX_HM: target_speaker registered EF sound noise='...' index=... spawnflags=... origin='...'`
- `STEFX_HM: server used EF target_speaker use spawnflags=... sound=...` (only appears if the speaker is activated during the match)
- `STEFX_HM: trigger_multiple using EF timing wait=... random=... target='...' origin='...'`
- `STEFX_HM: server used EF trigger activation path classname='trigger_multiple' target='...' wait=...`
- `STEFX_HM: server TeleportPlayer client=... from='...' to='...'` (first teleporter use)
- `STEFX_HM: server used EF teleporter destination clearance trace client=... fraction=... startsolid=... allsolid=...` (first teleporter destination placement)
- `STEFX_HM: cgame skipped inherited teleport spawn effect in Holomatch` (first teleporter visual event)
- `STEFX_HM: cgame skipped inherited predefined Force sound in Holomatch` (only appears if an old predefined sound event leaks into snapshots)
- `STEFX_HM: cgame skipped inherited NPC voice event in Holomatch event=...` (only appears if an old NPC custom voice event leaks)
- `STEFX_HM: cgame skipped inherited mode event in Holomatch event=...` (only appears if an old mode event leaks)
- `STEFX_HM: cgame skipped inherited model/duel event in Holomatch event=...` (only appears if an old model-mark or duel event leaks)
- `STEFX_HM: cgame skipped inherited holdable item event in Holomatch event=...` (only appears if an old holdable-use event leaks)
- `STEFX_HM: cgame ignored unknown Holomatch event event=... ent=... parm=... eType=... weapon=...` (only appears if an unhandled event reaches cgame)
- `STEFX_HM: cgame skipped Holomatch sound event with bad sound=... event=... ent=...` (only appears if an invalid sound payload reaches cgame)
- `STEFX_HM: cgame skipped Holomatch effect event with bad effect=... event=... ent=...` (only appears if an invalid effect payload reaches cgame)
- `STEFX_HM: cgame skipped Holomatch sound-set event with bad set=... event=... ent=...` (only appears if an invalid sound-set payload reaches cgame)
- `STEFX_HM: cgame skipped Holomatch client event with bad client=... event=... ent=...` (only appears if an invalid client payload reaches cgame)
- `STEFX_HM: cgame skipped Holomatch entity event with bad entity=... event=... ent=...` (only appears if an invalid entity payload reaches cgame)
- `STEFX_HM: cgame used EF taunt sound event client=...` (only appears if a taunt event reaches cgame with a loaded EF taunt)
- `STEFX_HM: cgame registered EF landing/fall media`
- `STEFX_HM: cgame registered EF footstep media`
- `STEFX_HM: cgame registered EF utility media`
- `STEFX_HM: server using EF fatal-fall media` (only appears if a fall kill occurs)
- `STEFX_HM: skipping inherited userinfo saber sync in Holomatch client=...`
- `STEFX_HM: skipping inherited saber entity init client=...`
- `STEFX_HM: skipping inherited spawn saber sync client=...`
- `STEFX_HM: skipping inherited bot saber setup client=...` (bot clients only)
- `STEFX_HM: scrubbed inherited client frame state in Holomatch client=... oldWeapon=...` (only appears if stale inherited player-state fields reach client frame processing)
- `STEFX_HM: skipping inherited saber stance selection in Holomatch client=...`
- `STEFX_HM: skipping inherited Force/Jedi visibility broadcasts in Holomatch`
- `STEFX_HM: skipping inherited generic command in Holomatch cmd=...` (only appears if an inherited generic command reaches normal Holomatch input processing)
- `STEFX_HM: cgame skipped saber setup client=...`
- `STEFX_HM: bot waypoint state reset`
- `STEFX_HM: inherited bot route data missing map='hm_borg1'; using fallback bot navigation`
- `STEFX_HM: fallback bot waypoints map='hm_borg1' total=... starts=... items=... connectors=... links=...`
- `STEFX_HM: addbot command name='1_of_12'`
- `STEFX_HM: addbot using EF aifile personality name='1_of_12' file='botfiles/bots/1of729_c.c'`
- `STEFX_HM: addbot accepted name='1_of_12'`
- `STEFX_HM: addbot scheduling begin name='1_of_12'`
- `STEFX_HM: bot setup client begin client=... personality='botfiles/bots/1of729_c.c' skill=4.00 team='free'`
- `STEFX_HM: bot setup personality begin client=... file='botfiles/bots/1of729_c.c'`
- `STEFX_HM: bot setup personality done client=... phaser=30 compression=100 imod=100 scavenger=100 tetrion=100 dreadnought=100`
- `STEFX_HM: bot EF weapon weights file='botfiles/bots/borg_w.c' phaser=30 compression=100 imod=100 scavenger=100 tetrion=100 dreadnought=100 mapped=6`
- `STEFX_HM: bot EF personality file='botfiles/bots/1of729_c.c' requestedSkill=4.0 selectedSkill=4 attack=0.70 aimSkill=0.75 aimAccuracy=0.75 view=0.60 maxturn=300 reaction=1.00 camper=0.30 weights='bots/borg_w.c' loadedWeights=1`
- `STEFX_HM: server used EF spawn selection classname='info_player_deathmatch' origin='...' avoid='...'`
- `STEFX_HM: ClientSpawn cleared inherited Force/saber state in Holomatch client=...`
- `STEFX_HM: ClientSpawn skipped inherited Force/saber animation setup in Holomatch client=...`
- `STEFX_HM: ClientSpawn skipped inherited Force/saber loadout branch in Holomatch client=...`
- `STEFX_HM: ClientSpawn Holomatch loadout client=... weapon=... ammo_phaser=... ammo_blaster=... standheight=32 crouchheight=16 defaultViewheight=26`
- `STEFX_HM: ClientBegin active state client=... bot=... sessionTeam=... persTeam=... weapon=... health=... armor=... invulnMs=...`
- `STEFX_HM: ClientSpawn Holomatch readied base weapon client=... weapon=... ammo_phaser=...`
- `STEFX_HM: ClientSpawn seeded EF idle animations client=... torso=... legs=...`
- `STEFX_HM: skipping inherited per-spawn Force setup in Holomatch client=...`
- `STEFX_HM: server skipped inherited force initialization in Holomatch client=...`
- `STEFX_HM: respawn used EF direct path client=...` (first post-death respawn)
- `STEFX_HM: ClientSpawn using EF ghost respawn protection client=... seconds=5`
- `STEFX_HM: EF ghost respawn protection blocked damage target=... mod=... dflags=...` (only appears if damage reaches a ghosted player before pickup/fire clears protection)
- `STEFX_HM: item pickup cleared EF ghost respawn protection client=... classname='...'` (only appears if a pickup happens while the spawn protection is still active)
- `STEFX_HM: action cleared EF ghost respawn protection client=... action='fire'` (only appears if firing clears spawn protection before pickup/timeout)
- `STEFX_HM: bot fallback weapon uses EF phaser bridge weapon=... client=...` (only appears if a bot runs out of ammo or reaches the inherited fallback selection path)
- `STEFX_HM: bot kept empty EF Phaser selectable weapon=... client=...` (only appears if a bot selector checks the empty Phaser recharge path)
- `STEFX_HM: bot treats EF Phaser bridge as normal combat weapon client=...` (appears once when bot fear logic first checks the Phaser bridge)
- `STEFX_HM: bot disabled inherited aim leading for EF Phaser bridge client=...` (appears once when bot aim logic first checks the Phaser bridge)
- `STEFX_HM: bot kept EF Phaser bridge through empty recharge combat path client=... ammo_phaser=...` (only appears if a bot reaches combat with an empty Phaser bridge)
- `STEFX_HM: bot corrected inherited melee/saber weapon to EF Phaser bridge client=... oldWeapon=...` (only appears if stale inherited weapon state reaches normal Holomatch bot AI)
- `STEFX_HM: bot skipped inherited Force-power decision path in Holomatch client=...` (appears once when normal Holomatch bot AI reaches the skipped inherited path)
- `STEFX_HM: bot skipped inherited saber/duel combat path in Holomatch client=...` (appears once when normal Holomatch bot AI reaches the skipped inherited path)
- `STEFX_HM: skipping inherited entity model attachment cleanup in Holomatch`
- `STEFX_HM: skipping inherited server player-model setup ent=...`
- `STEFX_HM: cgame using EF client-info marker load path`
- `STEFX_HM: cgame registered player marker client=... model='munro'`
- `STEFX_HM: cgame registered player marker client=... model='1_of_12'`
- `STEFX_HM: cgame registered player marker client=... model='2_of_3'`
- `STEFX_HM: cgame drew EF player marker client=... model='...'`
- `STEFX_HM: server defaulted Holomatch userinfo model client=... model='munro/default'` (only appears if model userinfo is missing or empty)
- `STEFX_HM: server sent EF clientinfo config client=... model='...' bot=... team=...`
- `STEFX_HM: ClientConnect forced Holomatch active FFA session client=... bot=...` (only appears if stale session data would otherwise leave a direct-boot client spectating)
- `STEFX_HM: ClientConnect Holomatch session client=... bot=... team=... firstTime=... newSession=...`
- `STEFX_HM: cgame loaded EF client sounds model='...' voice='...' fallback='hm_male'`
- `STEFX_HM: cgame skipped inherited player model memory free old='...' new='...'` (only appears if a client/bot model changes after an existing client-info slot has a model)
- `STEFX_HM: renderer bypassed inherited player model memory allocator for Holomatch model='...'` (only appears if a stale skeletal player model reaches renderer registration)
- `STEFX_HM: renderer bypassed inherited server model memory allocator for Holomatch model='...'` (only appears if a stale server-side skeletal player model reaches renderer registration)
- `STEFX_HM: cgame discarded inherited player model pointer client=...` (only appears if a stale pointer existed)
- `STEFX_HM: cgame discarded inherited entity model pointer client=...` (only appears if a stale pointer existed)
- `STEFX_HM: cgame discarded inherited saber model pointer client=... slot=...` (only appears if a stale pointer existed)
- `STEFX_HM: cgame clearing inherited model pointers without cleanup calls`
- `STEFX_HM: cgame prediction skipped inherited player model collision setup`
- `STEFX_HM: CG_RegisterWeapon EF media weapon=... class='...' item='...' view='...' flashModel=... barrelModel=...`
- `STEFX_HM: CG_AddViewWeapon using static EF hand frame weapon=...`
- `STEFX_HM: cgame drew EF beam fallback weapon=...`
- `STEFX_HM: cgame kept empty EF Phaser selectable weapon=...` (only appears when the Phaser reaches empty and remains selected/selectable)
- `STEFX_HM: shared movement emitted EF empty Phaser fire event weapon=... ammo_phaser=...` (only appears when the Phaser is fired empty)
- `STEFX_HM: server handled EF empty Phaser fire event client=... ammo_phaser=...` (only appears when the Phaser is fired empty or consumes the last primary charge)
- `STEFX_HM: cgame handled EF empty Phaser fire event`
- `STEFX_HM: cgame using EF obituary text bridge` (only appears after the first death/frag obituary reaches cgame)
- `STEFX_HM: cgame EF weapon command mapping request=... weapon=...` (only appears if the player uses an EF-numbered weapon command, or the internal hotswap path falls back from slot 1)
- `STEFX_HM: server initialized EF weapon stat tracking`
- `STEFX_HM: server using EF exit rules gametype=... fraglimit=... timelimit=...`
- `STEFX_HM: server sent EF score command clients=... total=...`
- `STEFX_HM: cgame parsed EF score command scores=...`
- `STEFX_HM: cgame used EF no-warmup fight sound` (only appears on a no-warmup map restart)
- `STEFX_HM: server used EF direct item pickup event item=... classname='...'` (first normal Holomatch pickup event)
- `STEFX_HM: cgame used EF direct pickup event item=...` (first normal Holomatch pickup event received by cgame)
- `STEFX_HM: weapon pickup client=... classname='...' weapon=... ammoIndex=... quantity=... ammoBefore=... ammoAfter=...` (first pickup for each bridged weapon slot)
- `STEFX_HM: cgame predicted EF weapon pickup classname='...' weapon=... ammoIndex=... quantity=... ammoBefore=... ammoAfter=...` (first predicted pickup for each bridged weapon slot)
- `STEFX_HM: cgame used EF pickup autoswitch path item=... weapon=... current=... autoswitch=... selected=...` (first local weapon pickup feedback path)
- `STEFX_HM: cgame used EF pickup display path item=... type=...` (first local non-weapon pickup feedback path if it happens before a weapon pickup)
- `STEFX_HM: cgame used EF no-ammo weapon fallback old=... alt=... selected=... switched=...` (only appears if a Holomatch weapon runs out of usable ammo or lacks alt-fire ammo)
- `STEFX_HM: armor pickup client=... classname='item_armor_combat' quantity=... tag=... armorBefore=... armorAfter=...` (first combat armor pickup)
- `STEFX_HM: shared movement skipped inherited rocket lock path for Holomatch` (only appears if the inherited lock path is entered)
- `STEFX_HM: server cleared inherited rocket lock state before Holomatch fire` (only appears if stale lock state reaches weapon fire)
- `STEFX_HM: cgame skipped inherited rocket lock HUD in Holomatch` (only appears if stale lock state reaches cgame)
- `STEFX_HM: cgame skipped inherited clash flare in Holomatch` (appears once when active HUD rendering first reaches the inherited flare hook)
- `STEFX_HM: server skipped inherited missile deflect effect weapon=...` (only appears if a bridged projectile hits an inherited deflect path)
- `STEFX_HM: server emitted explicit EF alternate missile entity weapon=... class='...'` (first Scavenger/Tetryon/Dreadnought alt projectile)
- `STEFX_HM: server Holomatch missile impact used EF normal damage weapon=...` (first Scavenger/Dreadnought projectile direct hit)
- `STEFX_HM: server skipped inherited missile player mark flag in Holomatch weapon=...` (first bridged projectile hit that would have carried an inherited player mark flag)
- `STEFX_HM: server removed inherited saber/model-part projectile weapon=...` (only appears if stale inherited projectile state reaches server missile think)
- `STEFX_HM: cgame skipped inherited saber-family event event=... in Holomatch` (only appears if a stale inherited saber-family event reaches cgame)
- `STEFX_HM: cgame handled EF Tetryon disruptor event without inherited effect` (only appears if Tetryon fire/impact/zoom feedback reaches cgame)
- `STEFX_HM: cgame rendered EF missile feedback without inherited projectile model weapon=...` (only appears if a live missile entity for a bridged EF weapon reaches cgame)
- `STEFX_HM: cgame rendered explicit EF alternate missile entity weapon=... ent=...` (only appears if an explicit EF alternate missile entity reaches cgame)
- `STEFX_HM: cgame treated EF alternate missile entity as projectile weapon=... ent=...` (only appears if an official EF alternate missile-shaped entity reaches cgame)
- `STEFX_HM: cgame skipped inherited saber/model-part projectile in Holomatch weapon=...` (only appears if stale inherited projectile state reaches cgame)
- `STEFX_HM: cgame skipped inherited vehicle impact effect path` (only appears if a missile impact checks the skipped vehicle-effect override path)
- `STEFX_HM: cgame handled EF I-MOD alt detonation without inherited effect` (only appears if I-MOD alt-fire detonates)
- `STEFX_HM: combat scrubbed inherited damage state role='...' client=... oldWeapon=...` (only appears if stale inherited combat state reaches damage handling)
- `STEFX_HM: combat skipped inherited shock damage modifier for Holomatch mod=...` (first I-MOD-backed Holomatch damage event)
- `STEFX_HM: combat skipped inherited explosion player mark in Holomatch weapon=... mod=...` (first radius hit that would have emitted an inherited player mark event)
- `STEFX_HM: skipping inherited cgame weapon model instances`
- `STEFX_HM: skipping inherited player weapon attachments`
- `STEFX_HM: cgame generic renderer scrubbed inherited model state` (only appears if stale inherited model state reaches a general entity)
- `STEFX_HM: cgame skipped inherited bolted entity render in Holomatch ent=...` (only appears if stale bolted-object state reaches a general entity)
- `STEFX_HM: cgame skipped inherited holocron model render in Holomatch ent=...` (only appears if an inherited holocron entity leaks)
- `STEFX_HM: cgame skipped inherited body fade/disintegration render in Holomatch ent=...` (only appears if stale inherited body effects leak)
- `STEFX_HM: cgame skipped inherited flying saber visual in Holomatch ent=...` (only appears if stale flying-saber state leaks)
- `STEFX_HM: cgame skipped inherited holocron visual in Holomatch ent=...` (only appears if stale inherited holocron effect state leaks)
- `STEFX_HM: cgame skipped inherited tripmine/Force-sight visual in Holomatch ent=...` (only appears if stale inherited tripmine visual state leaks)
- `STEFX_HM: FireWeapon begin client=... weapon=... alt=... ammo_phaser=... ammo_blaster=... ammo_powercell=... ammo_rockets=...`
- `STEFX_HM: server EF fire bridge weapon=... class='phaser' alt=...`
- `STEFX_HM: server EF fire bridge weapon=... class='compression' alt=...`
- `STEFX_HM: server EF fire bridge weapon=... class='imod' alt=...`
- `STEFX_HM: server EF fire bridge weapon=... class='scavenger' alt=...`
- `STEFX_HM: server EF fire bridge weapon=... class='tetryon' alt=...`
- `STEFX_HM: server EF fire bridge weapon=... class='dreadnought' alt=...`
- `STEFX_HM: FireWeapon end client=... weapon=... alt=... ammo_phaser=... ammo_blaster=... ammo_powercell=... ammo_rockets=...`
- `STEFX_HM: player death cleared inherited saber/Force state in Holomatch` (only appears after the first player death)
- `STEFX_HM: server using EF death animation cycle in Holomatch` (only appears after the first player death)
- `STEFX_HM: skipping inherited limb break path in Holomatch client=... arm=...` (only appears if stale body-damage logic is reached)
- `STEFX_HM: cgame skipped inherited ragdoll callback in Holomatch type=...` (only appears if a stale ragdoll callback is requested)
- `STEFX_HM: skipping inherited corpse/body queue in Holomatch path` (only appears if a player death/corpse path is reached)
- `STEFX_HM: skipping inherited server player animation path in Holomatch` (only appears if the death/combat animation path is reached)
- `STEFX_HM: skipping inherited dismemberment path in Holomatch` (only appears if the dismemberment path is reached)
- `STEFX_HM: skipping inherited server model cleanup in Holomatch`
- `STEFX_HM: skipping inherited cgame model cleanup in Holomatch`
- `STEFX_HM: skipping inherited shared UI model cleanup in Holomatch`
- `STEFX_HM: server ignored inherited model cleanup request ent=...` (only appears if stale server cleanup state reaches Holomatch)
- `STEFX_HM: server dropped inherited model cleanup queue count=...` (only appears if stale queued cleanup state reaches Holomatch)
- `STEFX_HM: server cleared stale inherited model cleanup flag ent=...` (only appears if stale entity cleanup state reaches Holomatch)
- `STEFX_HM: server cleared stale inherited entity model pointer ent=...` (only appears if stale entity model state reaches Holomatch)
- `STEFX_HM: server cleared stale inherited weapon model pointer ent=... slot=...` (only appears if stale weapon model state reaches Holomatch)
- `STEFX_HM: server emitted EF trace impact event weapon=... alt=... event=...` (only appears once an EF traced weapon hits map or player geometry)
- `STEFX_HM: cgame ignored inherited model cleanup command in Holomatch` (only appears if the server sends an inherited model cleanup command)
- `STEFX_HM: cgame ignored inherited corpse restore command in Holomatch` (only appears if a stale restore/body-copy command reaches cgame)
- `STEFX_HM: cgame ignored inherited model cleanup event in Holomatch` (only appears if a stale model or dropped-weapon cleanup event reaches cgame)
- `STEFX_HM: shared snapshot scrubbed inherited Force/saber/vehicle state client=...` (only appears if stale inherited snapshot state reaches shared entity-state conversion)
- `STEFX_HM: G_SpawnEntitiesFromString done map='hm_borg1'`

## 2026-07-16 UI/DDS Mandate Checkpoint

- Holomatch MP UI build surface is SP/EF-only: `x_ui` compiles `codemp/ui/ui_stefx_spbridge.cpp` as syscall/export glue plus `code/ui/ui_ef_lifecycle.cpp`, `code/ui/ui_ef_frontend.cpp`, `code/ui/ui_ef_pause.cpp`, and `code/ui/ui_ef_qmenu.cpp`.
- Deprecated MP menu parser/runtime calls are intercepted by the bridge/shim as unsupported SP/EF UI routes; old MP menu strings are not allowed in the staged XBE.
- Active Holomatch UI/HUD/player asset code no longer asks for explicit JPG/TGA/PNG filenames; EF UI assets and player icons use extensionless DDS-native requests.
- Fresh staged package proof: `xbox1.pk3` reports `ddsOnly=true`, `alphaTextureFormat=bgra32`, `textureCount=4618`, `preservedOriginalTextures=0`, `uiScriptCount=0`, and contains zero JPG/TGA/PNG image entries.
- Fresh staged XBE proof: new SP EF UI/HUD strings are present, `models/players2/%s/icon_%s.jpg` and old MP menu strings are absent.
- Runtime log remains stale until the next CXBX-R run; do not treat the old missing HUD/body checkpoints as current evidence.

## 2026-07-16 Current Vertical Slice Baseline

- Latest staged MP artifacts are `efmp.xbe` timestamped 2026-07-16 18:59:06 and `BaseEF\xbox1.pk3` timestamped 2026-07-16 19:00:49. Staged `default.xbe` is still timestamped 2026-07-13 19:05:50 and was not touched.
- `xbox1.pk3` remains DDS-only and now forces all packaged `gfx/` UI/HUD imagery to BGRA32 DDS. Current package proof: 4618 DDS images, zero non-DDS images, zero UI parser scripts, 267 `gfx/` textures all BGRA32, 22 `gfx/interface` HUD textures all BGRA32, and 1040 Holomatch support files.
- `scripts\check_mp_holomatch_ui.py` now also requires the shared EF/SP `gfx/interface` HUD DDS assets in `xbox1.pk3`, so missing SP interface HUD art is a package failure.
- `scripts\build_xbox_patch_pk3.py` now treats `gfx/interface` as a HUD texture seed and forces `gfx/` textures to BGRA32 DDS instead of DXT-compressed UI art.
- Holomatch player body drawing now checks torso/head attach tags explicitly. Missing tags produce `STEFX_HM: cgame EF player attach tag missing ...` and use a visible attach offset instead of silently collapsing the body.
- `hm_borg1` material audit against `build\release\BaseEF` and `xbox1.pk3` passes with 76 used materials, zero missing assets, zero unresolved used materials, and 11 expected additive/transparent/black effect materials.
- `scripts\audit_ef_bsp_materials.py` now treats `xbox1.pk3` as a patch authority, not only `xbox0.pk3`, so MP DDS overrides are included in material-resolution audits.
- Runtime log remains stale at 2026-07-16 10:14:05 until the next CXBX-R run; do not use that old log as evidence for the current body/HUD/UI package state.

## 2026-07-16 UI Uniformity + Drivability Proof Checkpoint

- The normal MP build now calls `scripts\check_mp_holomatch_ui.py` with `--xbe` for both repo output and CXBX-R stage output. The gate passes only when `x_ui` is SP/EF-only, `xbox1.pk3` has zero UI parser scripts, staged `BaseEF` has zero loose UI parser scripts, and `efmp.xbe` has zero old MP menu markers.
- Current staged `efmp.xbe` proof strings: `STEFX_HM: SP EF UI lifecycle initialized from code/ui`, `STEFX_HM: cgame SP EF HUD shim`, `STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path`, `STEFX_HM: score update client=`, `STEFX_HM: player death scored`, and `STEFX_HM: respawn used EF direct path`.
- Current staged `efmp.xbe` negative proof: `inherited MP menu`, `JA MP menu`, `ui/jamp/main.menu`, `ui/jampmenus.txt`, `ui/jampingame.txt`, `menu/new/bar1.tga`, and `menu/common/warpcore2.jpg` are absent.
- `scripts\check_mp_holomatch_log.ps1` now accepts `-Log` and `-Xbe` aliases, treats `queueing direct Holomatch Borg bots` and `queueing direct Holomatch bots` as equivalent, and requires score-update, death-score, and respawn-loop breadcrumbs in a fresh runtime log.
- `AddScore`, `player_die`, and the EF direct respawn branch now emit score/death/respawn breadcrumbs so a fresh CXBX-R run can prove the bot match loop is actually playable, not merely spawned.
- Rebuilt with `powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp`; build and stage completed, UI mandate gate passed for repo and CXBX-R stage outputs, and `hm_borg1` material audit still reports zero missing/unresolved used materials.

## 2026-07-16 SP/EF UI Lifecycle Mandate Checkpoint

- The active MP UI entrypoint now keeps `codemp/ui` as syscall/export glue only. UI lifecycle ownership moved under `code/ui`: `code/ui/ui_ef_lifecycle.cpp` owns init/cache, refresh, key routing, active-menu routing, unsupported parser-menu rejection, and `Menus_*` shims for Holomatch MP.
- `x_ui.vcproj`, `scripts/build_xbox.ps1`, and `scripts/check_mp_holomatch_ui.py` now require `../../code/ui/ui_ef_lifecycle.cpp`; unapproved `code/ui` additions must be added explicitly to the gate.
- Binary proof after the rebuild: staged `efmp.xbe` contains `STEFX_HM: SP EF UI lifecycle initialized from code/ui`; it does not contain the old bridge-initialized marker, `inherited MP menu`, `ui/jamp/main.menu`, or `ui/jampmenus.txt`.
- Staged artifact proof after the rebuild: repo and CXBX-R `efmp.xbe` are `5,554,176` bytes timestamped `2026-07-16 18:59:06`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 19:00:49`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Gates run after the lifecycle rebuild: staged UI/XBE gate passed with zero packaged/staged UI scripts and zero legacy XBE strings; `hm_borg1` material audit passed with `used_missing_assets=0` and `used_unresolved_materials=0`.

## 2026-07-16 SP/EF HUD Startup + DDS-Only Stage Checkpoint

- Holomatch cgame now arms the EF SP interface HUD startup state when HUD graphics register: graphic/number pieces start off, health/armor/ammo timers are set, and the HUD completes through `STEFX_HM: EF SP interface HUD startup complete ...` before steady-state drawing.
- CXBX-R staging now removes every loose `.tga`/`.jpg`/`.jpeg`/`.png` fallback covered by a DDS entry in `BaseEF\xbox1.pk3`; the latest stage removed 4618 loose original texture fallbacks.
- Fresh staged proof after `build_mp_ui_uniform_dds_stage.log`: `efmp.xbe` is timestamped `2026-07-16 19:15:08`, `BaseEF\xbox1.pk3` is timestamped `2026-07-16 19:17:20`, and `default.xbe` remains timestamped `2026-07-13 19:05:50`.
- Package/stage proof: `xbox1.pk3` contains 4618 DDS entries, zero original JPG/TGA/PNG entries, and staged `BaseEF` contains zero loose original image files after cleanup.
- Gates run after this rebuild: SP/EF-only UI gate passed, staged XBE contains the UI lifecycle plus HUD startup markers and no JA menu strings, and `hm_borg1` material audit still reports `used_missing_assets=0` and `used_unresolved_materials=0`.
- Runtime log remains stale at `2026-07-16 10:14:05`; the next CXBX-R run is required before treating HUD/body/score checkpoints as current runtime evidence.

## 2026-07-16 DDS Format Policy Gate Checkpoint

- `scripts\check_mp_holomatch_ui.py` now enforces the Holomatch DDS policy, not just UI policy: package DDS-only manifest, `bgra32` alpha policy, zero preserved originals, zero skipped alpha textures, zero packaged JPG/TGA/PNG files, and zero loose original image files in the CXBX-R runtime stage.
- The same checker now reads DDS headers directly and permits only `DXT1`, `RGB565`, and `BGRA32`; `DXT5` or any unknown DDS format is a package failure. All packaged `gfx/` UI/HUD entries must be `BGRA32`.
- Build workspace checks may pass `--allow-stage-original-images` because `build\release\BaseEF` remains the source-image workspace used to regenerate `xbox1.pk3`; CXBX-R stage checks do not use this flag.
- Fresh package format proof after the gate change: 4618 DDS entries total, with `DXT1=3664`, `BGRA32=947`, `RGB565=7`, and no `DXT5`.
- Fresh build proof after `build_mp_dds_gate_policy.log`: normal MP build/stage passed, staged `efmp.xbe` is timestamped `2026-07-16 19:25:57`, staged `BaseEF\xbox1.pk3` is timestamped `2026-07-16 19:28:35`, and staged `default.xbe` remains timestamped `2026-07-13 19:05:50`.
- `scripts\check_mp_holomatch_log.ps1` now also requires fresh runtime breadcrumbs for `STEFX_HM: EF SP interface HUD startup armed` and `STEFX_HM: EF SP interface HUD startup complete`, so the next CXBX-R log proves the SP HUD lifecycle actually ran.

## 2026-07-16 UI Uniformity Mandate + Renderer Probe Checkpoint

- Holomatch MP UI now emits `STEFX_HM: MP UI bridge is syscall adapter only; UI behavior is shared code/ui` from `codemp/ui/ui_stefx_spbridge.cpp`; `scripts\check_mp_holomatch_ui.py` requires that marker in `efmp.xbe` alongside the `code/ui` lifecycle marker.
- Live EF UI path terminology was scrubbed so the active qmenu/lifecycle route describes unsupported legacy script UI rather than implying a parallel parser-menu system.
- `code/win32/openjkdf2/fakeglx.cpp` now keeps separate pre-present and post-present framebuffer telemetry budgets and samples the first 16 post-present swaps without requiring a screenshot request.
- `scripts\check_mp_holomatch_log.ps1` now requires fresh runtime proof for `STEFX: fakegl framebuffer sample ... afterPresent=1` and `STEFX: fakegl SwapBuffers ... Present hr=0x00000000`.
- Fresh staged artifact proof: repo and CXBX-R `efmp.xbe` are `5,554,176` bytes timestamped `2026-07-16 19:41:03`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 19:43:25`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS gate passed with `xUiSources=[ui_stefx_spbridge.cpp, code/ui/ui_ef_frontend.cpp, code/ui/ui_ef_pause.cpp, code/ui/ui_ef_qmenu.cpp, code/ui/ui_ef_lifecycle.cpp]`, zero packaged UI scripts, zero staged UI scripts, zero loose original runtime images, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log is still stale at `2026-07-16 10:14:05` versus the staged `efmp.xbe` timestamp `2026-07-16 19:41:03`; do not treat missing HUD/body/score/post-present checkpoints as current until a fresh CXBX-R run writes a new log.

## 2026-07-16 Shared SP/EF Qmenu Framework Header Checkpoint

- The EF qmenu framework types now live in `code/ui/ui_ef_qmenu_shared.h`. `code/ui/ui_local.h` and the MP syscall adapter header `codemp/ui/ui_stefx_spcompat.h` both include that shared header instead of carrying separate menu-structure definitions.
- `codemp/ui/ui_stefx_spcompat.h` now keeps only MP adapter state/import declarations plus shared `code/ui` includes; the duplicated `menuframework_s`, `menucommon_s`, `menubitmap_s`, `menutext_s`, and related qmenu definitions were removed from `codemp/ui`.
- `codemp/x_ui/x_ui.vcproj` lists `../../code/ui/ui_ef_qmenu_shared.h`, and `scripts/check_mp_holomatch_ui.py` now requires that header as part of the SP/EF-only UI contract.
- Fresh staged artifact proof after rebuilding: repo and CXBX-R `efmp.xbe` are `5,554,176` bytes timestamped `2026-07-16 19:54:21`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 19:56:47`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Repo and CXBX-R UI/DDS gates passed after this rebuild. The staged runtime package still has zero UI scripts, zero loose original image files, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 19:54:21`; the next CXBX-R run is still required for current HUD/body/score/render proof.

## 2026-07-16 Runtime Log Sink Marker Checkpoint

- `codemp/win32/xb_log.cpp` now logs the concrete runtime sink path on startup with `STEFX_HM: efmp.xbe runtime log sink path='...' primary='D:\ef_mp_log.txt' fallback='E:\ef_mp_log.txt' build='...'`.
- The log path behavior remains D: first and E: fallback. If both fail, the raw NT partition fallback is still used, and the marker reports `raw-nt-partition1`.
- `scripts\check_mp_holomatch_log.ps1` now requires the `efmp.xbe runtime log sink` marker in fresh logs, which makes stale logs easier to distinguish from current CXBX-R output.
- The same runtime checker now requires a nonblank post-present framebuffer sample: `STEFX: fakegl framebuffer sample ... afterPresent=1 ... nonzero=[1-9]`, so a black/empty presentation cannot satisfy the renderer checkpoint.
- Fresh staged artifact proof after rebuilding: repo and CXBX-R `efmp.xbe` are `5,554,176` bytes timestamped `2026-07-16 20:04:34`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 20:06:47`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Binary proof: staged `efmp.xbe` contains `STEFX_HM: efmp.xbe runtime log sink`, `D:\ef_mp_log.txt`, and `E:\ef_mp_log.txt`.
- Staged UI/DDS gate and repo UI/DDS gate both passed after this rebuild. `hm_borg1` material audit still reports `used_missing_assets=0` and `used_unresolved_materials=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 20:04:34`; the stale log correctly misses the new runtime marker.

## 2026-07-16 Match Heartbeat + Strict Runtime Stage Checkpoint

- `codemp/game/g_main.c` now emits a low-rate Holomatch FFA heartbeat from the server frame loop after exit-rule checks. The marker is `STEFX_HM: match heartbeat ... connected=... playing=... active=... bots=... alive=... topClient=... topScore=... totalScore=...`.
- The same heartbeat pass logs up to six active player/bot state lines with `STEFX_HM: match client state client=... bot=... team=... health=... armor=... score=... weapon=... ammo_phaser=... origin='...' velocity='...' name='...'`.
- `scripts\check_mp_holomatch_log.ps1` now requires both heartbeat markers in fresh logs, in addition to the existing bot-accepted, HUD, renderer, score, death, respawn, and body checkpoints.
- Rebuilt MP with `powershell -ExecutionPolicy Bypass -File scripts\build_xbox.ps1 -Target mp`. Staged artifact proof after the rebuild: CXBX-R `efmp.xbe` is `5,558,272` bytes timestamped `2026-07-16 20:16:41`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 20:18:57`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Binary proof: staged `efmp.xbe` contains `STEFX_HM: match heartbeat`, `STEFX_HM: match client state`, `STEFX_HM: MP UI bridge is syscall adapter only`, and `STEFX_HM: efmp.xbe runtime log sink`.
- CXBX-R stage UI/DDS gate passed with zero packaged UI scripts, zero staged UI scripts, zero packaged original image entries, zero loose runtime JPG/TGA/PNG fallbacks, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Repo package UI/DDS gate now checks the package and XBE without treating the source-image workspace as a runtime stage. Source JPG/TGA/PNG files remain build inputs for regenerating DDS; the actual package and CXBX-R stage remain DDS-only.
- `scripts\build_xbox.ps1` no longer passes `--allow-stage-original-images` for the repo package/XBE gate. The strict no-fallback stage gate remains the CXBX-R stage gate after `efmp.xbe` and `xbox1.pk3` are copied.
- `hm_borg1` material audit after this rebuild still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 20:16:41`. The stale log correctly misses the new runtime sink marker, heartbeat markers, HUD/body/render markers, and later score/death/respawn markers until a fresh CXBX-R run writes a new log.

## 2026-07-16 Cgame HUD Bypass + EF Missile Impact Route Checkpoint

- `codemp/cgame/cg_draw.c` now logs `STEFX_HM: cgame Holomatch 2D using SP interface-only path; legacy cgame parser HUD bypassed` from the Holomatch 2D branch before drawing the EF SP interface HUD. This branch returns before the inherited cgame HUD/menu draw stack.
- `scripts\check_mp_holomatch_ui.py` now verifies the cgame Holomatch HUD bypass source markers and reports `cgameHolomatchHudBypass=true`. The staged XBE check also requires the new HUD bypass runtime marker.
- `scripts\check_mp_holomatch_log.ps1` now requires the HUD bypass marker in fresh logs, so a runtime smoke cannot pass if the old cgame parser HUD path is active.
- The stale runtime log from `2026-07-16 10:14:05` ended after `STEFX_HM: cgame skipped inherited vehicle impact effect path`. That is not current proof of the crash point, but it identified a weak branch for instrumentation and cleanup before the next run.
- `codemp/cgame/cg_event.c` now handles `EV_MISSILE_HIT`, `EV_MISSILE_MISS`, and `EV_MISSILE_MISS_METAL` through a Holomatch-only EF missile impact route before the inherited event switch can run old impact effects. The route logs `STEFX_HM: cgame EF missile impact feedback begin ...` and `STEFX_HM: cgame EF missile impact feedback end ...`.
- If a replicated missile-impact event reaches cgame without a recognized EF weapon value, the new route logs `STEFX_HM: cgame defaulted Holomatch missile impact feedback weapon ...` and uses the EF Phaser feedback instead of falling into inherited effects.
- `scripts\check_mp_holomatch_log.ps1` now requires `STEFX_HM: cgame EF missile impact feedback end event=...` in a fresh runtime log.
- Fresh staged artifact proof after rebuilding: CXBX-R `efmp.xbe` is `5,562,368` bytes timestamped `2026-07-16 20:36:25`; staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 20:38:35`; staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Binary proof: staged `efmp.xbe` contains the HUD bypass marker, `STEFX_HM: cgame EF missile impact feedback begin`, `STEFX_HM: cgame EF missile impact feedback end`, and the missile-impact fallback marker.
- CXBX-R stage UI/DDS gate passed after this rebuild with `cgameHolomatchHudBypass=true`, zero UI scripts, zero loose runtime JPG/TGA/PNG fallbacks, zero packaged original image entries, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 20:36:25`; the next CXBX-R run is still required to verify current HUD, renderer, body, bot, score, missile-impact, death, and respawn checkpoints.

## 2026-07-16 UI Uniformity Mandate Clarification

- UI uniformity is a mandate for Holomatch, not a suggested cleanup. The active MP UI behavior must live in shared SP/EF `code/ui` code, with `codemp/ui` acting only as the MP syscall/export adapter.
- Inherited JA/JAMP menu source may remain only as guarded dead code that hard-errors under `STEFX_ELITE_FORCE_MP`; it must not compile into `efmp.xbe`, own VM command routing, load `.menu` scripts, or provide active HUD/menu behavior.
- `scripts/check_mp_holomatch_ui.py` now reports the policy as `mandated-shared-ef-sp-code-ui-owns-behavior` and forbids additional legacy parser/menu markers in `efmp.xbe`, including old `jk2mp` menu files, `ui/hud.txt`, JA load-screen art, radar menu art, and stale statusbar art.

## 2026-07-16 Shared EF Cgame HUD Layout Checkpoint

- The EF interface HUD layout table now lives in shared `code/cgame/cg_ef_hud_shared.h`, using the official EF `gfx/interface` cap/bar layout instead of a private MP table in `codemp/cgame`.
- `codemp/cgame/cg_draw.c` includes that shared header for the active Holomatch HUD path, preserving the parser-HUD bypass while making the live HUD layout SP/EF-owned.
- `scripts/check_mp_holomatch_ui.py` now requires the shared HUD layout include and fails if the `stefxHolomatchHud` layout table is reintroduced directly into `codemp/cgame`.
- Fresh build proof after `build_mp_shared_ef_cgame_hud.log`: staged `efmp.xbe` is timestamped `2026-07-16 21:43:19`, staged `BaseEF\xbox1.pk3` is timestamped `2026-07-16 21:45:00`, and staged `default.xbe` remains timestamped `2026-07-13 19:05:50`.
- Stage gate proof: zero packaged UI scripts, zero staged UI scripts, zero loose runtime JPG/TGA/PNG fallbacks, DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`, and `sharedEfCgameHudLayout=../../code/cgame/cg_ef_hud_shared.h`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 21:43:19`; the next CXBX-R run is still required for current HUD/body/render/bot/scoring proof.

## 2026-07-16 MP Renderer EF Overlay Parity Checkpoint

- `codemp/renderer/tr_shade.cpp` now applies the SP-style EF overlay state for Holomatch HUD/crosshair and beam shaders in both multitexture and single-stage draw paths: depth test/write off, alpha test off, appropriate alpha/additive blend, texture stage reset to stage 0, and draw-context tagging into the shared Xbox draw shim.
- The new runtime breadcrumbs in `efmp.xbe` are `STEFX_HM: renderer EF overlay state`, `STEFX_HM: renderer EF overlay D3D state`, `STEFX_HM: renderer EF overlay prepare`, and `STEFX_HM: renderer EF overlay draw`.
- Fresh staged artifact proof after `build_mp_renderer_overlay_parity_2.log`: staged `efmp.xbe` is timestamped `2026-07-16 21:56:23`, staged `BaseEF\xbox1.pk3` is timestamped `2026-07-16 21:58:20`, and staged `default.xbe` remains timestamped `2026-07-13 19:05:50`.
- Binary proof: staged `efmp.xbe` contains the four renderer EF overlay markers plus `STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path`; it still does not contain `ui/hud.txt`, `gfx/menus/newFront/SaberLoad`, or `ui/assets/statusbar/selectedhealth.tga`.
- Stage gate proof after this rebuild: zero packaged UI scripts, zero staged UI scripts, zero packaged original image entries, zero loose runtime JPG/TGA/PNG fallbacks, DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`, and `sharedEfCgameHudLayout=../../code/cgame/cg_ef_hud_shared.h`.
- `hm_borg1` material audit after this rebuild still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`, with the expected effect materials resolved from `xbox1.pk3`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 21:56:23`; a fresh CXBX-R run is still required for current HUD/body/render/bot/scoring proof.

## 2026-07-16 Shared SP UI Framework Ownership Checkpoint

- The UI uniformity mandate is now represented in the active `x_ui` build, not just in policy text: Holomatch MP compiles `../../code/ui/ui_atoms.cpp` and `../../code/ui/ui_main.cpp` alongside the shared EF frontend/pause/qmenu/lifecycle sources.
- `codemp/ui/ui_stefx_spbridge.cpp` is reduced to syscall/export adapter scope and emits `STEFX_HM: MP UI bridge is syscall adapter only; SP code/ui owns UI framework state and rendering`. The bridge no longer owns `uis`, `uiInfo`, text painting, or draw helper implementations.
- Shared `code/ui` now owns the MP-visible UI framework state under the Holomatch guard. Runtime proof markers are `STEFX_HM: SP UI atoms active in efmp.xbe; MP bridge owns only syscall plumbing` and `STEFX_HM: SP UI main framework active in efmp.xbe; text/UI state owned by shared code/ui`.
- The staged `efmp.xbe` also contains `STEFX_HM: renderer using SP-style top-left 2D projection`, and `scripts/check_mp_holomatch_ui.py` now requires that marker so the shared EF HUD layout cannot regress to the old MP bottom-left 2D projection that put HUD elements at the top of the screen.
- Fresh staged artifact proof after `build_mp_sp_ui_framework_stage.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 22:27:22`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 22:29:11`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Stricter staged gate proof after `check_mp_holomatch_ui_after_sp_2d_projection_gate.txt`: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `xUiSources=[../ui/ui_stefx_spbridge.cpp, ../../code/ui/ui_atoms.cpp, ../../code/ui/ui_ef_frontend.cpp, ../../code/ui/ui_ef_pause.cpp, ../../code/ui/ui_ef_qmenu.cpp, ../../code/ui/ui_ef_lifecycle.cpp, ../../code/ui/ui_main.cpp]`, `mpCompiledSpUiFrameworkSources=[code/ui/ui_atoms.cpp, code/ui/ui_main.cpp]`, `rendererSpStyle2DProjection=true`, zero packaged/staged UI scripts, zero loose runtime original images, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `scripts/check_mp_holomatch_log.ps1` now requires the renderer projection marker in fresh CXBX-R logs. The current log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 22:27:22`; do not treat missing runtime HUD/body/render/scoring checkpoints as current until a fresh run writes `ef_mp_log.txt`.

## 2026-07-16 Cgame Menu Asset Cache Scrub Checkpoint

- `codemp/cgame/cg_main.c` now skips the inherited cgame menu asset cache under `STEFX_ELITE_FORCE_MP` and emits `STEFX_HM: cgame skipped inherited menu asset cache for shared SP UI path`. This prevents Holomatch from registering the old MP menu slider art after the shared SP UI route is active.
- `scripts/check_mp_holomatch_ui.py` requires the new source/XBE marker, and `scripts/check_mp_holomatch_log.ps1` requires the marker in a fresh runtime log.
- Fresh build/stage proof after `build_mp_skip_cgame_menu_asset_cache.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 22:40:11`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 22:42:10`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS gate passed after the asset-cache scrub with `rendererSpStyle2DProjection=true`, shared SP UI framework ownership intact, zero packaged/staged UI scripts, zero loose runtime original images, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Local package preview from staged `xbox1.pk3` (`build\hud_preview\ef_hud_native.png`, `ef_hud_flip_v.png`, `ef_hud_flip_uv.png`) shows the native DDS orientation is the correct bottom-corner EF HUD orientation; V-flipped and U+V-flipped variants invert the cap cutouts. Do not flip HUD texture coordinates unless a fresh runtime screenshot/log proves the current staged binary still disagrees.
- `hm_borg1` material audit after the asset-cache scrub (`audit_hm_borg1_after_asset_cache_skip.txt`) still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 22:40:11`; the stale log correctly misses the new asset-cache breadcrumb.

## 2026-07-16 EF-Native Local Player Model + Reject-Only Cgame UI Shim Checkpoint

- Holomatch local/default player identity is now EF-native: `codemp/client/cl_main.cpp` defaults the userinfo model to `munro/default`, `codemp/client/cl_data.cpp` seeds `ClientManager` with `munro/default`, and `codemp/cgame/cg_players.c` keeps the direct fallback model at `munro`.
- `scripts/check_mp_holomatch_ui.py` now requires the EF-native model markers in source and staged `efmp.xbe`, reporting `holomatchDefaultPlayerModel=munro/default`.
- The cgame UI compatibility shim remains reject-only under the UI uniformity mandate. `codemp/cgame/cg_stefx_ui_shim.c` logs `STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus ...`, while `CG_LoadHudMenu` still returns before loading `ui/hud.txt` and `CG_LoadMenus` refuses deprecated parser HUD loads under `STEFX_ELITE_FORCE_MP`.
- Fresh build/stage proof after `build_mp_ef_default_player_model.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 22:52:29`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 22:54:08`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS gate passed after the EF-native player model change with `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `mpCompiledSpUiFrameworkSources=[code/ui/ui_atoms.cpp, code/ui/ui_main.cpp]`, zero packaged/staged UI scripts, zero loose runtime original images, `rendererSpStyle2DProjection=true`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after the EF-native player model change (`audit_hm_borg1_after_ef_default_player_model.txt`) still reports `materials=81`, `used=76`, `used_missing_assets=0`, `used_unresolved_materials=0`, and `expected_fx_or_black=11`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 22:52:29`; the next CXBX-R run is still required for current HUD/body/render/bot/scoring proof.

## 2026-07-16 Reject-Only Cgame UI Shim Binary Gate Checkpoint

- `scripts/check_mp_holomatch_ui.py` now treats the cgame UI parser shim as part of the UI uniformity contract: it requires `cgameUiShimRejectOnly=true`, the reject-only marker in `codemp/cgame/cg_stefx_ui_shim.c`, and binary markers proving `CG_LoadHudMenu` disables the parser HUD and `CG_LoadMenus` refuses deprecated parser HUD loads.
- `scripts/check_mp_holomatch_log.ps1` now requires fresh runtime breadcrumbs for `STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus` and `STEFX_HM: cgame HUD menu system disabled for EF MP; using EF SP interface HUD`.
- The rebuild command for `build_mp_cgame_ui_shim_reject_only.log` outlived the command wrapper timeout, so there is no wrapper exit code to report. The artifacts did finish staging and the post-build proof was run against the staged files.
- Fresh staged artifact proof after the reject-only shim marker rebuild: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 23:01:06`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 23:02:45`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the reject-only shim marker rebuild: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `cgameUiShimRejectOnly=true`, `legacyStringCount=0`, zero packaged/staged UI scripts, zero loose runtime original images, `holomatchDefaultPlayerModel=munro/default`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Binary proof from staged `efmp.xbe`: the reject-only cgame UI shim marker is present, the cgame HUD-menu disabled marker is present, and the deprecated MP HUD menu-load refusal marker is present.
- `hm_borg1` material audit after the reject-only shim marker rebuild (`audit_hm_borg1_after_cgame_ui_reject_only.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 23:01:06`; the stale log correctly misses the new cgame UI shim and HUD-menu-disabled checkpoints.

## 2026-07-16 EF Score Overlay Parser-Scoreboard Bypass Checkpoint

- `codemp/cgame/cg_draw.c` now routes `CG_DrawScoreboard()` through `CG_STEFXDrawHolomatchScores()` under `STEFX_ELITE_FORCE_MP`, so intermission, death, and direct scoreboard draw paths use the EF Holomatch score overlay instead of `CG_DrawOldScoreboard()` or parser scoreboard menus.
- `codemp/cgame/cg_consolecmds.c` now ignores parser scoreboard scroll commands under Holomatch and logs `STEFX_HM: cgame ignored parser scoreboard scroll; EF score overlay owns scores` rather than touching menu feeders.
- `scripts/check_mp_holomatch_ui.py` now requires `cgameScoreboardParserBypass=true`, the EF score overlay route marker, the parser scoreboard scroll refusal marker, and both corresponding strings in staged `efmp.xbe`.
- `scripts/check_mp_holomatch_log.ps1` now requires `STEFX_HM: cgame scoreboard uses EF Holomatch overlay; parser scoreboard bypassed` in fresh runtime logs.
- Fresh build/stage proof after `build_mp_scoreboard_parser_bypass.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 23:18:55`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 23:20:52`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the scoreboard bypass: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `cgameScoreboardParserBypass=true`, `cgameUiShimRejectOnly=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Binary proof from staged `efmp.xbe`: `STEFX_HM: cgame scoreboard uses EF Holomatch overlay; parser scoreboard bypassed` and `STEFX_HM: cgame ignored parser scoreboard scroll; EF score overlay owns scores` are both present.
- `hm_borg1` material audit after the scoreboard bypass (`audit_hm_borg1_after_scoreboard_bypass.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 23:18:55`; the stale log correctly misses the new scoreboard parser-bypass checkpoint.

## 2026-07-16 Cgame Parser Entry Points Dead Checkpoint

- `codemp/cgame/cg_main.c` now rejects direct deprecated `loadmenu` blocks under `STEFX_ELITE_FORCE_MP` with `STEFX_HM: cgame ignored deprecated MP loadmenu block; shared SP UI owns menus`.
- `CG_SetScoreSelection()` still updates the selected score index for live cgame data, but under Holomatch it no longer writes into parser menu feeders. The runtime proof marker is `STEFX_HM: cgame score selection stayed data-only; parser menu feeders ignored`.
- `CG_LoadHudMenu()` now emits `STEFX_HM: cgame parser entry points are dead; shared SP UI owns menus` on the active init path, so fresh logs can prove the policy without needing an intentionally dead helper to be called.
- `scripts/check_mp_holomatch_ui.py` now requires `cgameParserEntryPointsDead=true` in source and staged `efmp.xbe`, in addition to the existing shared SP UI framework, cgame shim, HUD bypass, and scoreboard bypass checks.
- `scripts/check_mp_holomatch_log.ps1` now requires the active parser-entry-points-dead init marker in fresh CXBX-R logs.
- Fresh build/stage proof after `build_mp_cgame_parser_dead_runtime_marker.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 23:37:56`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 23:39:41`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the parser-entry-point hard gate: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `cgameParserEntryPointsDead=true`, `cgameScoreboardParserBypass=true`, `cgameUiShimRejectOnly=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, `holomatchDefaultPlayerModel=munro/default`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_parser_dead_runtime_marker.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 23:37:56`; the stale log correctly misses the new active parser-entry-points-dead checkpoint.

## 2026-07-16 SP-Style Xbox Input Device Init Checkpoint

- `codemp/win32/win_main_console.cpp` now follows the SP/OpenJKDF2 device ordering for Holomatch MP: `XInitDevices` runs before D3D/fakegl setup, then `g_XInitDevicesAlreadyCalled` is set so `IN_Init()` cannot call it a second time.
- The legacy MP `Direct3D_SetPushBufferSize(1024*1024, 128*1024)` call is removed from the main path. MP now logs `STEFX_HM: MP using SP-style fakegl pushbuffer path; main skipped legacy Direct3D_SetPushBufferSize`, matching SP ownership of fakegl pushbuffer setup.
- `codemp/win32/win_input_xbox.cpp` now uses the shared early-init flag, registers `joy_deadzone` with SP's `0.18` default, clamps the cvar to `[0.0, 0.95]`, logs the gamepad mask/deadzone, and logs the first raw gamepad state per port.
- Existing MP active-client and split-screen controller activation code is left intact; this pass changes initialization, deadzone behavior, and diagnostics only.
- `scripts/check_mp_holomatch_ui.py` now reports `inputSpEarlyDeviceInit=true` and `inputJoyDeadzoneDefault=0.18`, requires the input markers in source and staged `efmp.xbe`, and fails if the old MP main pushbuffer call returns.
- `scripts/check_mp_holomatch_log.ps1` now requires fresh runtime checkpoints for early `XInitDevices`, the SP input device path, and first gamepad state.
- Fresh build/stage proof after `build_mp_input_sp_device_path.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-16 23:45:49`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-16 23:47:31`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the input change: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `inputSpEarlyDeviceInit=true`, `inputJoyDeadzoneDefault=0.18`, `cgameParserEntryPointsDead=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_input_sp_device_path.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 23:45:49`; the stale log correctly misses the new input checkpoints.

## 2026-07-16 EF/SP Sound Handle Guard Checkpoint

- `codemp/client/snd_dma_console.cpp` now uses EF/SP-hardened Xbox sound guards for sound handles and entity numbers. The old one-past-end checks are replaced with helpers that reject `sfxHandle >= MAX_SFX` and `entityNum >= MAX_GENTITIES`.
- `S_Init()` now emits `STEFX_HM: sound using EF/SP hardened Xbox path; handle/entity guards active`, and sound registration emits `STEFX_HM: sound registration active listeners=...`.
- Local sounds now validate the active listener index before indexing the listener array, preserving split-screen plumbing while preventing bad active-client state from walking off the listener table.
- `scripts/check_mp_holomatch_ui.py` now reports `soundEfSpHardenedGuards=true`, requires the guard markers in source and staged `efmp.xbe`, and fails if the old `sfxHandle > MAX_SFX` or `entityNum > MAX_GENTITIES` patterns return.
- Fresh build/stage proof after `build_mp_sound_handle_guards.log`: staged `efmp.xbe` was `5,566,464` bytes timestamped `2026-07-16 23:55:49`, staged `BaseEF\xbox1.pk3` was `78,322,721` bytes timestamped `2026-07-16 23:57:36`, and staged `default.xbe` remained `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the sound guard pass with `soundEfSpHardenedGuards=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_sound_handle_guards.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remained stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-16 23:55:49`; the stale log correctly missed the new sound checkpoints.

## 2026-07-17 UI Mandate No Script-Cache Checkpoint

- The shared Holomatch MP UI lifecycle in `code/ui/ui_ef_lifecycle.cpp` now logs `STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache; codemp/ui remains adapter-only`.
- `scripts/check_mp_holomatch_ui.py` now fails if the shared Holomatch UI lifecycle re-enters parser menu loading/cache calls such as direct `Menu_Cache`, `UI_LoadMenus`, or `UI_ParseMenu`. EF-owned frontend/pause cache calls remain allowed.
- The same checker also rejects parser-menu calls if they creep into `codemp/ui/ui_stefx_spbridge.cpp`, preserving that file as syscall/export adapter plumbing only.
- `scripts/check_mp_holomatch_log.ps1` now requires the no-script-cache lifecycle marker in fresh CXBX-R logs, alongside the shared SP UI VM dispatch marker.
- Fresh build/stage proof after `build_mp_ui_no_script_cache.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 00:05:34`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 00:07:38`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the mandate hardening: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `mpVmDispatchInSharedCodeUi=true`, `cgameParserEntryPointsDead=true`, `soundEfSpHardenedGuards=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_ui_no_script_cache.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 00:05:34`; a fresh CXBX-R run is still required before treating missing runtime checkpoints as current.

## 2026-07-17 Cgame Parser HUD Guard Checkpoint

- Dormant inherited cgame HUD helpers now refuse parser HUD work under Holomatch if they are accidentally reached. `CG_DrawVehicleHud()` logs `STEFX_HM: cgame skipped inherited parser vehicle HUD; EF Holomatch HUD owns status` and returns to the player-HUD path; `CG_DrawStats()` logs `STEFX_HM: cgame skipped inherited parser stats HUD; EF Holomatch HUD owns status` and returns.
- This preserves split-screen variables and future split-screen plumbing while preventing old menu-backed vehicle/status HUD lookups from becoming active behavior in `efmp.xbe`.
- `scripts/check_mp_holomatch_ui.py` now requires both guard strings in source and staged `efmp.xbe`, in addition to the main Holomatch 2D route, cgame parser-entry rejection, and EF score overlay checks.
- Fresh build/stage proof after `build_mp_cgame_parser_hud_guards.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 00:13:09`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 00:14:58`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the parser HUD guard pass: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `cgameHolomatchHudBypass=true`, `cgameParserEntryPointsDead=true`, `cgameScoreboardParserBypass=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_cgame_parser_hud_guards.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 00:13:09`; the new parser-HUD guard strings are staged but need a fresh CXBX-R run before runtime status can be claimed.

## 2026-07-17 Renderer Solid-Fill Reset Checkpoint

- `codemp/renderer/tr_backend.cpp` now forces Xbox front and back fill mode to `D3DFILL_SOLID` at `RB_BeginDrawingView()` and through the 2D overlay state reset. This removes a class of stale wireframe-state leakage before world and HUD presentation.
- `codemp/renderer/tr_shade.cpp` now applies the same solid-fill reset to EF overlay/HUD/beam draw paths, next to the existing EF overlay depth/blend/cull state reset.
- New runtime proof markers are `STEFX_HM: renderer forced Xbox solid fill mode where=...` and `STEFX_HM: renderer EF overlay solid fill mode where=...`.
- `scripts/check_mp_holomatch_ui.py` now reports `rendererSolidFillReset=true`, requires the solid-fill source markers, and requires both marker strings in staged `efmp.xbe`. `scripts/check_mp_holomatch_log.ps1` now requires the world-view solid-fill marker in fresh CXBX-R logs.
- Fresh build/stage proof after `build_mp_renderer_solid_fill.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 00:20:51`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 00:22:42`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the renderer reset: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `rendererSolidFillReset=true`, `rendererSpStyle2DProjection=true`, `cgameParserEntryPointsDead=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_renderer_solid_fill.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 00:20:51`; a fresh CXBX-R run is still required to verify current visuals and runtime checkpoints.

## 2026-07-17 EF MDR Frame Clamp Checkpoint

- `codemp/renderer/tr_animation.cpp` now clamps EF MDR animation frames at the renderer boundary before culling, fog lookup, and surface submission. Bad or wrapped frame values are kept inside `[0, numFrames - 1]` instead of collapsing every out-of-range body part to frame zero.
- The clamp logs `STEFX_HM: renderer clamped EF MDR frame model=...` when it corrects a frame, giving CXBX-R/runtime logs a direct breadcrumb if an EF `animation.cfg` row or network state feeds an invalid frame into body drawing.
- The same renderer tracking now recognizes EF body paths under `models/players2/`, so fresh logs can show animated EF body surface enter/cull/visible breadcrumbs for Holomatch player and bot bodies instead of only watching inherited `models/players/` paths.
- `scripts/check_mp_holomatch_ui.py` now reports `rendererMdrFrameClamp=true`, requires the clamp source markers, and requires the clamp marker string inside staged `efmp.xbe`.
- Fresh build/stage proof after `build_mp_mdr_players2_tracking.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 00:38:23`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 00:40:07`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the clamp: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `rendererMdrFrameClamp=true`, `rendererSolidFillReset=true`, `rendererSpStyle2DProjection=true`, `cgameParserEntryPointsDead=true`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_mdr_players2_tracking.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 00:38:23`; no current CXBX-R runtime proof should be claimed until a fresh run writes `ef_mp_log.txt`.

## 2026-07-17 Shared SP UI Framework File Checkpoint

- `codemp/x_ui/x_ui.vcproj` now compiles `../../code/ui/ui_shared.cpp` into `x_ui` alongside `ui_atoms.cpp`, `ui_main.cpp`, and the EF frontend/pause/qmenu/lifecycle files. This removes the last project-level gap where MP was compiling only part of the SP UI framework.
- Under `STEFX_ELITE_FORCE_MP`, `code/ui/ui_shared.cpp` owns the parser compatibility entry points (`Menus_ActivateByName`, `Menus_OpenByName`, `Menus_CloseAll`, `Menu_PaintAll`, `Item_RunScript`, etc.) and keeps them reject-only or routed into EF/SP code menus. The proof markers are `STEFX_HM: SP UI shared framework compiled in efmp.xbe; parser compatibility is reject-only` and `STEFX_HM: SP UI shared framework rejected script menu route op=...`.
- `code/ui/ui_ef_lifecycle.cpp` no longer carries duplicate fallback menu-entry definitions. The lifecycle still owns VM dispatch and no-script-cache initialization; shared `ui_shared.cpp` owns the compatibility menu symbols.
- `scripts/check_mp_holomatch_ui.py` now requires `../../code/ui/ui_shared.cpp` in the active MP `x_ui` source list, reports `mpCompiledSpUiFrameworkSources=[code/ui/ui_atoms.cpp, code/ui/ui_main.cpp, code/ui/ui_shared.cpp]`, and requires both shared-framework marker strings in staged `efmp.xbe`.
- Fresh build/stage proof after `build_mp_sp_ui_shared_framework_2.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 00:53:07`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 00:55:14`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the shared-framework file inclusion: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `xUiSources=[../ui/ui_stefx_spbridge.cpp, ../../code/ui/ui_atoms.cpp, ../../code/ui/ui_ef_frontend.cpp, ../../code/ui/ui_ef_pause.cpp, ../../code/ui/ui_ef_qmenu.cpp, ../../code/ui/ui_ef_lifecycle.cpp, ../../code/ui/ui_main.cpp, ../../code/ui/ui_shared.cpp]`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_sp_ui_shared_framework.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 00:53:07`; no current CXBX-R runtime proof should be claimed until a fresh run writes `ef_mp_log.txt`.

## 2026-07-17 Ad Hoc MP HUD Status Text Removal Checkpoint

- `codemp/cgame/cg_draw.c` no longer draws the top-center ad hoc MP weapon/score strip inside the EF SP interface HUD path. Health, armor, and ammo remain owned by the shared EF interface HUD table, and scores remain owned by the EF Holomatch score overlay.
- The removed helper/string pair was `CG_STEFXHolomatchWeaponName()` plus the `"%s  SCORE %d"` draw. This keeps the active Holomatch HUD from layering inherited MP-style status text over the EF interface system.
- Fresh runtime proof marker added for the active HUD path: `STEFX_HM: cgame skipped ad hoc MP weapon/score HUD text; EF interface and score overlay own status`.
- `scripts/check_mp_holomatch_ui.py` now requires that marker, fails if the removed helper or status format returns to `codemp/cgame/cg_draw.c`, and also forbids the removed status format/helper name from staged `efmp.xbe`.
- Fresh build/stage proof after `build_mp_hud_uniform_status.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 01:02:59`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 01:04:40`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the HUD status cleanup: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `cgameHolomatchHudBypass=true`, `cgameScoreboardParserBypass=true`, `xUiSources=[../ui/ui_stefx_spbridge.cpp, ../../code/ui/ui_atoms.cpp, ../../code/ui/ui_ef_frontend.cpp, ../../code/ui/ui_ef_pause.cpp, ../../code/ui/ui_ef_qmenu.cpp, ../../code/ui/ui_ef_lifecycle.cpp, ../../code/ui/ui_main.cpp, ../../code/ui/ui_shared.cpp]`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_hud_uniform_status.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 01:02:59`; no current CXBX-R runtime proof should be claimed until a fresh run writes `ef_mp_log.txt`.

## 2026-07-17 HUD V-Corrected DDS UV Checkpoint

- The UI mandate is now treated as a hard project rule: Holomatch MP uses the shared EF/SP `code/ui` framework uniformly. Inherited JA/JAMP menu systems are dead code for this target; `codemp/ui` is limited to syscall/export adapter plumbing for `efmp.xbe`.
- `codemp/cgame/cg_draw.c` keeps the EF interface HUD on the shared SP HUD table, but its texture draw helper now uses a DDS V-corrected UV path for the interface pieces after CXBX-R showed the HUD vertically flipped. The runtime marker is `STEFX_HM: cgame EF SP interface HUD using DDS V-corrected UV draw path`.
- The V correction is scoped to EF interface HUD drawing only. It does not globally flip renderer UVs, and it does not remove or flatten split-screen state; future split-screen work should still route through the same shared EF/SP UI ownership.
- `scripts/check_mp_holomatch_ui.py` now requires the V-corrected HUD marker in source and staged `efmp.xbe`, rejects the old native-UV HUD marker, and continues to fail on packaged/staged JA menu scripts or legacy MP HUD status strings.
- Superseded by the later native-DDS HUD result. Current source, staged `efmp.xbe`, and `scripts/check_mp_holomatch_log.ps1` require `STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path` and treat the V-corrected marker as the old flipped path.
- Fresh build/stage proof after `build_mp_hud_vfix.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 01:13:30`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 01:15:21`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the HUD V fix: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `legacyStringCount=0`, `stageUiScripts=0`, `stageOriginalImages=0`, `ddsFormats=[DXT1=3664, BGRA32=947, RGB565=7]`, and active `x_ui` sources are `../ui/ui_stefx_spbridge.cpp` plus shared `../../code/ui/*.cpp` UI framework files.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_hud_vfix.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 01:13:30`; no current CXBX-R runtime proof should be claimed until a fresh run writes `ef_mp_log.txt`.

## 2026-07-17 Uniform SP UI Mandate Checkpoint

- `code/ui/ui_ef_lifecycle.cpp` now emits an active runtime proof marker from the shared UI lifecycle: `STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI`.
- `scripts/check_mp_holomatch_ui.py` now requires that marker in source and staged `efmp.xbe`, and its output explicitly reports `uiMandateUniformSpCodeUi=true`, `mpCodempUiBehaviorSources=[../ui/ui_stefx_spbridge.cpp]`, and `mpLegacyUiBehaviorSources=0`.
- `scripts/check_mp_holomatch_log.ps1` now requires the uniform-SP-UI mandate marker in fresh CXBX-R logs. The current log is still intentionally treated as stale until CXBX-R is run again.
- Fresh build/stage proof after `build_mp_ui_mandate_uniform.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 01:22:18`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 01:24:08`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the mandate marker build: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `uiMandateUniformSpCodeUi=true`, `mpLegacyUiBehaviorSources=0`, `stageUiScripts=0`, `stageOriginalImages=0`, `originalImageEntries=0`, `legacyStringCount=0`, and DDS formats are `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Active `x_ui` sources remain `../ui/ui_stefx_spbridge.cpp`, `../../code/ui/ui_atoms.cpp`, `../../code/ui/ui_ef_frontend.cpp`, `../../code/ui/ui_ef_pause.cpp`, `../../code/ui/ui_ef_qmenu.cpp`, `../../code/ui/ui_ef_lifecycle.cpp`, `../../code/ui/ui_main.cpp`, and `../../code/ui/ui_shared.cpp`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_ui_mandate_uniform.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 01:22:18`; no current runtime proof should be claimed until a fresh CXBX-R run writes `ef_mp_log.txt`.

## 2026-07-17 Phaser Bot-Combat Drivability Checkpoint

- `codemp/game/g_weapon.c` now raises the Holomatch Phaser baseline from 6 to 20 damage and enforces an 8 damage minimum after range/ammo scaling. This is scoped to `STEFX_ELITE_FORCE_MP` and is intended to make the default bot weapon lethal enough for a vertical-slice match instead of only proving impact events.
- The Phaser path now emits bounded runtime damage breadcrumbs: `STEFX_HM: server EF Phaser applied damage attacker=... target=... damage=... healthBefore=... healthAfter=... armorBefore=... armorAfter=... targetFlags=...`. This should distinguish real health/armor damage from ghost-respawn protection or other blocked damage.
- Existing Holomatch death, score, and respawn proof markers remain required: `STEFX_HM: score update client=...`, `STEFX_HM: player death scored ...`, and `STEFX_HM: respawn used EF direct path ...`.
- `scripts/check_mp_holomatch_ui.py` now reports `combatPhaserDamageProof=true` and requires the Phaser damage marker in source and staged `efmp.xbe`. `scripts/check_mp_holomatch_log.ps1` now requires the runtime Phaser damage marker before a fresh run can count as a complete drivable combat proof.
- Fresh build/stage proof after `build_mp_combat_phaser_damage.log`: staged `efmp.xbe` is `5,566,464` bytes timestamped `2026-07-17 01:33:41`, staged `BaseEF\xbox1.pk3` is `78,322,721` bytes timestamped `2026-07-17 01:35:45`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the combat patch: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `uiMandateUniformSpCodeUi=true`, `mpLegacyUiBehaviorSources=0`, `combatPhaserDamageProof=true`, `stageUiScripts=0`, `stageOriginalImages=0`, `originalImageEntries=0`, `legacyStringCount=0`, and DDS formats are `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- `hm_borg1` material audit after this rebuild (`audit_hm_borg1_after_combat_phaser_damage.txt`) still reports `materials=81`, `used=76`, `missing=0`, and `unresolved=0`.
- Runtime log remains stale at `2026-07-16 10:14:05` against staged `efmp.xbe` timestamp `2026-07-17 01:33:41`; the stale log correctly misses the new `Phaser damage applied` checkpoint, so no current runtime kill/respawn proof should be claimed until CXBX-R writes a fresh `ef_mp_log.txt`.

## 2026-07-17 Official EF AAS Botlib Load Checkpoint

- The botlib map path now restores the AAS lifecycle that was present in the tree but disconnected from the exported hooks: `Export_BotLibSetup()` calls `AAS_Setup()`, `Export_BotLibLoadMap()` calls `AAS_LoadMap(mapname)`, `Export_BotLibStartFrame()` calls `AAS_StartFrame(time)`, and shutdown calls `AAS_Shutdown()`.
- `codemp/game/ai_main.c` now follows the official EF `BotAILoadMap()` behavior for fresh map loads by calling `trap_BotLibLoadMap(mapname.string)` after probing `maps/hm_borg1.aas`. The generated waypoint route remains available as the Xbox fallback because the inherited JA bot movement code still has waypoint dependencies.
- Runtime proof markers added: `STEFX_HM: official EF AAS botlib load begin map='hm_borg1'` and `STEFX_HM: official EF AAS botlib load result=0 map='hm_borg1'`.
- `scripts/check_mp_holomatch_log.ps1` now requires the successful botlib AAS load marker and treats `STEFX_HM: official EF AAS botlib load failed` or `AAS_LoadMap failed` as forbidden.

## 2026-07-17 Official AAS + Local Fallback Route Runtime Checkpoint

- `scripts/build_xbox_patch_pk3.py` now patches the packaged `maps/hm_borg1.aas` header checksum in `xbox1.pk3` to match the optimized Xbox BSP checksum. EF AAS v5 stores the header with the standard byte obfuscation, so the packer writes the encoded header bytes while the decoded checksum remains `439350207`.
- `scripts/check_mp_holomatch_ui.py` verifies the manifest `patchedAasChecksums` entry, decodes the staged AAS header checksum from `xbox1.pk3`, and confirms it matches the optimized BSP checksum.
- Runtime now proves the official AAS load path succeeds: `STEFX_HM: official EF AAS botlib checksum var set value='439350207'`, `loaded maps/hm_borg1.aas`, and `STEFX_HM: official EF AAS botlib load result=0 map='hm_borg1'`.
- The generated Holomatch waypoint fallback no longer enters the inherited game-side `CalculatePaths()` or server trap path calculation. Both stopped CXBX-R after official AAS loaded. The fallback now builds plain local Holomatch waypoint links and logs `STEFX_HM: fallback bot waypoint local links done map='hm_borg1' total=64 links=32`.
- Fresh build/stage proof after `build_mp_local_fallback_links.log`: staged `efmp.xbe` is `5,677,056` bytes timestamped `2026-07-17 12:20:52`, staged `BaseEF\xbox1.pk3` is `78,323,176` bytes timestamped `2026-07-17 12:22:41`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Fresh CXBX-R log smoke after this route fix passed all vertical-slice checkpoints: `hm_borg1` running, local client active, both Borg bots accepted, HUD draw active, match heartbeat, combat weapon path, score update, death scored, and respawn loop.
- `scripts/check_mp_holomatch_log.ps1` also passed against the fresh log and now requires the AAS load, inherited path skip, local links, and trap skip markers while forbidding the old trap path entry marker.

## 2026-07-17 Bot Primary-Fire Stability Soak Checkpoint

- A longer CXBX-R run exposed a bot stability issue after a Borg picked up `weapon_scavenger` and fired an EF alternate projectile. The shorter smoke could pass, but the 240-second soak died after the alternate missile was rendered.
- `codemp/game/ai_main.c` now clamps automated EF Holomatch bot weapon output to primary fire for the current vertical slice. The weapon code remains in place for later manual/player projectile work, but Borg bot AI no longer chooses EF alternate fire during the automated match.
- New proof markers are `STEFX_HM: bot disabled EF Holomatch alternate fire for vertical-slice stability weapon=...` and `STEFX_HM: bot converted EF Holomatch alternate fire command to primary fire weapon=...`.
- Fresh build/stage proof after `build_mp_bot_primary_fire_only.log`: staged `efmp.xbe` is `5,677,056` bytes timestamped `2026-07-17 12:33:55`, staged `BaseEF\xbox1.pk3` is `78,323,176` bytes timestamped `2026-07-17 12:35:39`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Fresh short CXBX-R smoke after the bot clamp passed all vertical-slice checkpoints and logged the bot alternate-fire clamp marker.
- Fresh 240-second hidden CXBX-R soak after the bot clamp also passed: `hm_borg1` stayed running with one local client and two Borg bots, HUD/render/present heartbeats continued, combat/death/respawn markers remained present, and no explicit EF alternate missile path appeared.
- `scripts/check_mp_holomatch_log.ps1` passed against the 240-second soak log.
- `hm_borg1` material audit after this build (`audit_hm_borg1_after_bot_primary_fire_only.txt`) reports `materials=81`, `used=76`, `used_missing_assets=0`, and `used_unresolved_materials=0`.
- Superseded by the EF alternate-projectile safe-sprite checkpoint below. Bot alternate fire should stay enabled; the renderer/cgame projectile path owns the stability fix.

## 2026-07-17 EF Alternate Projectile Safe-Sprite Checkpoint

- The temporary bot primary-fire-only workaround was removed from `codemp/game/ai_main.c`. Bot command generation again preserves `BUTTON_ALT_ATTACK`, so automated Holomatch combat is not being stabilized by suppressing alternate fire.
- `codemp/cgame/cg_ents.c` now handles EF Holomatch missiles with a safe `RT_SPRITE` feedback path and skips per-frame moving-missile dlights on the Xbox renderer. New proof markers are `STEFX_HM: cgame skipped EF moving missile dlight on Xbox renderer weapon=... alt=...`, `STEFX_HM: cgame rendered EF alternate missile safe sprite weapon=... ent=... radius=...`, and `STEFX_HM: cgame rendered EF missile feedback without inherited projectile model weapon=...`.
- The UI mandate is treated as a hard project rule in the current gates: shared EF/SP `code/ui` owns Holomatch UI behavior, `codemp/ui` remains adapter-only, JA/JAMP menu scripts are absent from staged `BaseEF`, and old parser/menu runtime strings remain absent from `efmp.xbe`.
- `scripts/check_mp_holomatch_ui.py` now requires the safe projectile markers in source and staged `efmp.xbe`, and rejects the old bot primary-fire-only workaround markers in both source and `efmp.xbe`.
- `scripts/check_mp_holomatch_log.ps1` now also forbids the old bot alternate-fire primary-only workaround in fresh CXBX-R logs.
- Fresh build/stage proof after `build_mp_alt_projectile_safe_sprite.log`: staged `efmp.xbe` is `5,677,056` bytes timestamped `2026-07-17 12:50:38`, staged `BaseEF\xbox1.pk3` is `78,323,176` bytes timestamped `2026-07-17 12:52:22`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after the projectile fix: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `uiMandateUniformSpCodeUi=true`, `mpCodempUiBehaviorSources=[../ui/ui_stefx_spbridge.cpp]`, `mpLegacyUiBehaviorSources=0`, `stageUiScripts=0`, `stageOriginalImages=0`, `originalImageEntries=0`, `legacyStringCount=0`, and DDS formats are `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Fresh short CXBX-R smoke after the projectile fix passed all vertical-slice checkpoints: `hm_borg1` running, local client active, both Borg bots accepted, HUD draw active, render/present proof, combat weapon path, score update, death scored, and respawn loop.
- Fresh 240-second hidden CXBX-R soak after the projectile fix also passed with alternate fire enabled: `hm_borg1` stayed running with one local client and two Borg bots, HUD/render/present heartbeats continued through the run, combat/death/respawn markers remained present, and the log checker reported `no bot alternate-fire primary-only workaround`.
- The latest 240-second log picked up `weapon_scavenger` but did not happen to fire the alternate projectile during that run; the current source and staged `efmp.xbe` still contain the safe alternate-projectile path, and the old workaround strings are absent.
- `hm_borg1` material audit after this build (`audit_hm_borg1_after_alt_projectile_safe_sprite.txt`) reports `materials=81`, `used=76`, `used_missing_assets=0`, and `used_unresolved_materials=0`.

## 2026-07-17 Menu Status / Direct Map Boot Checkpoint

- User visual review showed the shared EF/SP menu framework is structurally present but visually broken: fonts and layout render incorrectly enough that menus are not acceptable as part of the current Holomatch vertical slice.
- Current slice policy: `efmp.xbe` must boot straight into `hm_borg1` for testing. Menus remain future work under the uniform SP `code/ui` mandate, not a runtime dependency for the current match proof.
- `codemp/win32/win_main_console.cpp` now bakes `+map hm_borg1` and `+set r_uiFullScreen 0` into the Holomatch MP startup command, while leaving the older main-loop `map hm_borg1` queue as a fallback if the startup command path does not start the server.
- New runtime marker: `STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line`.
- Fresh build/stage proof after `build_mp_direct_map_boot.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 14:27:36`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 14:29:46`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Fresh CXBX-R smoke after direct-map boot (`smoke_cxbx_mp_log_direct_map_boot.txt`) passed the runtime checkpoints: startup command includes `+map hm_borg1`, the new menu-bypass marker is present, `SV_SpawnServer begin hm_borg1` happens before frame 1, `hm_borg1` stays running, both Borg bots join, the HUD draws, Phaser damage applies, scoring/death occurs, and respawn uses the EF direct path.
- `scripts/check_mp_holomatch_log.ps1` now accepts the new direct startup bypass marker as the primary `hm_borg1` proof while still accepting the older delayed map queue marker as a fallback.

## 2026-07-17 BGRA32 Direct Upload / No-Fraglimit Checkpoint

- User manual CXBX-R run froze after live combat and showed an RGB/BGR channel swap. The latest log proved `hm_borg1` reached active play with two Borg bots, scoring, death, and respawn before the stall; no new CXBX-R instance was launched while investigating.
- The renderer color fix is in `code/win32/openjkdf2/glteximage_dds.cpp`: `GL_DDS_RGBA32_EXT` now routes through the direct Xbox DDS upload path for Holomatch MP instead of converting packaged BGRA32 DDS payloads through the generic RGBA upload path. The new proof marker is `STEFX_HM: BGRA32 DDS direct Xbox upload ...`.
- The Holomatch direct boot command in `codemp/win32/win_main_console.cpp` now uses `+set fraglimit 0` instead of `+set fraglimit 10`, avoiding end-match/intermission code while the current vertical slice is supposed to stay in a drivable match.
- Fresh build/stage proof after `build_mp_bgra32_direct_fraglimit0.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 15:06:05`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 15:08:10`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after this rebuild: `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, `uiMandateUniformSpCodeUi=true`, `mpLegacyUiBehaviorSources=0`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats are `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- Binary string proof on staged `efmp.xbe`: `+set fraglimit 0` is present, `+set fraglimit 10` is absent, `+map hm_borg1` is present, and the BGRA32 direct-upload proof marker is present.
- No runtime proof is claimed for this checkpoint yet. The user needs to run the newly staged `efmp.xbe` in CXBX-R and check whether colors are corrected and the match no longer freezes shortly after combat starts.

## 2026-07-17 SP Renderer Texture Policy Checkpoint

- Holomatch MP now ports the compatible SP Xbox texture-upload policy into `codemp/renderer/tr_image_xbox.cpp`: regular RGBA uploads use the SP cap rules, EF UI fonts can remain 256px, EF player body textures under `models/players2/` are capped like SP player textures, and known Borg alpha cutout textures preserve `GL_RGBA8` instead of being demoted to 16-bit alpha.
- Holomatch MP renderer init now follows SP Xbox defaults in `codemp/renderer/tr_init.cpp`: dynamic glow is forced off, `r_picmip` is forced to `1`, `r_texturebits` is forced to `0` so fakegl receives 3/4 component internal formats, `r_stefxLightmapBoost` is registered at `2.5`, and `r_subdivisions` uses the SP Xbox `64` value.
- `scripts/check_mp_holomatch_ui.py` now enforces this renderer parity as `rendererSpTexturePolicy=true` and requires both source markers and staged `efmp.xbe` strings, including `STEFX_HM: MP renderer using SP Xbox Upload32 caps`, `STEFX_HM: MP renderer using SP Xbox r_texturebits=0 component upload policy`, and `STEFX_HM: BGRA32 DDS direct Xbox upload`.
- Fresh build/stage proof after `build_mp_sp_renderer_texture_policy.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 15:17:55`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 15:19:50`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after this rebuild with `rendererSpTexturePolicy=true`, `rendererSpScreenUploadPath=true`, `rendererSolidFillReset=true`, `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- No CXBX-R runtime proof is claimed for this checkpoint yet. The current `ef_mp_log.txt` still predates the staged renderer-policy build.

## 2026-07-17 SP Input Direct-Map Gate Checkpoint

- `codemp/win32/win_input_xbox.cpp` now restores the MP-side no-controller tracker instead of leaving it commented out. It does not invoke any old JA UI menu; it only keeps the shared SP-style controller state coherent and logs `STEFX_HM: input SP no-controller tracking active`.
- MP gamepad reads now zero-initialize `XINPUT_STATE`, check the `XInputGetState` result, and log `STEFX_HM: input state read failed port=...` before using the cleared state. This prevents stale thumbstick/buttons from surviving a failed read while preserving the existing split-screen per-port activation loop.
- Direct Holomatch map boot now clears the splash/controller lock cvars on the first input frame when `stefx_hm_directSlice` is active, matching the SP direct-map input gate. Proof marker: `STEFX_HM: direct-map input gate cleared splash/controller lock`.
- `scripts/check_mp_holomatch_ui.py` now requires those input markers in source and staged `efmp.xbe`, in addition to the existing early `XInitDevices` and `joy_deadzone=0.18` SP-device path markers.
- Fresh build/stage proof after `build_mp_input_sp_direct_gate_rerun.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 15:34:31`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 15:36:10`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after this rebuild with `inputSpEarlyDeviceInit=true`, `inputJoyDeadzoneDefault=0.18`, `rendererSpTexturePolicy=true`, `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- No CXBX-R runtime proof is claimed for this checkpoint yet. An unrelated CXBX-R instance for another project was left untouched during this build. Future visual-proof capture should use `C:\Games\Emulators\CXBX-CodexCapture`; read that folder's `AGENTS.md` before using it.

## 2026-07-17 SP Sound Attenuation / Listener Policy Checkpoint

- `codemp/client/snd_dma_console.cpp` now uses the EF/SP Xbox 3D sound attenuation baseline: `SOUND_REF_DIST_BASE` is `1500.f` instead of the inherited JA `150.f`. This keeps Holomatch 3D audio falloff aligned with the cooperative executable while leaving MP's two-listener split-screen capacity in place.
- MP sound registration now clamps listener counts through `S_STEFXClampListenerCount()` before allocating OpenAL sources. This preserves one or two active listeners but prevents zero/over-limit listener counts from reaching the source allocator.
- New XBE/source proof markers are `STEFX_HM: sound using SP Xbox attenuation reference distance=...` and `STEFX_HM: sound clamped listener count caller=...`.
- `scripts/check_mp_holomatch_ui.py` now reports `soundSpAttenuationPolicy=true` and requires the SP attenuation/listener policy markers in source and staged `efmp.xbe`.
- Fresh build/stage proof after `build_mp_sound_sp_attenuation_policy.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 15:45:33`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 15:47:36`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after this rebuild with `soundEfSpHardenedGuards=true`, `soundSpAttenuationPolicy=true`, `inputSpEarlyDeviceInit=true`, `rendererSpTexturePolicy=true`, `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- No CXBX-R runtime proof is claimed for this checkpoint yet. An unrelated CXBX-R instance for another project was left untouched.

## 2026-07-17 Exact SP 2D Renderer Wholesale Checkpoint

- User clarified that wholesale means copied exactly, line by line. The MP 2D backend functions `RB_SetGL2D`, `RB_StretchPic`, `RB_RotatePic`, and `RB_RotatePic2` in `codemp/renderer/tr_backend.cpp` now match the SP `code/renderer/tr_backend.cpp` function bodies exactly after newline normalization.
- The only MP-specific glue for those copied branches is file-local build glue in `scripts/build_xbox.ps1`: `codemp\renderer\tr_backend.cpp` receives `STEFX_ELITE_FORCE_SP` as a source-level compiler definition while the rest of `efmp.xbe` remains the MP build.
- `scripts/check_mp_holomatch_ui.py` now compares those four MP renderer function bodies directly against the SP bodies instead of accepting the old Holomatch-specific 2D marker string. It also requires the SP renderer proof strings `STEFX: RB_XboxForce2DOverlayState`, `STEFX_FRONTEND_2D_BACKEND`, and `STEFX: RB_StretchPic` in staged `efmp.xbe`.
- Fresh build/stage proof after `build_mp_renderer_sp_wholesale_exact.out.log`: staged `efmp.xbe` is `5,681,152` bytes timestamped `2026-07-17 16:23:10`, staged `BaseEF\xbox1.pk3` is `78,342,111` bytes timestamped `2026-07-17 16:24:57`, and staged `default.xbe` remains `4,907,008` bytes timestamped `2026-07-13 19:05:50`.
- Staged UI/DDS/XBE gate passed after this rebuild with `rendererSp2DWholesaleFunctions=[RB_SetGL2D, RB_StretchPic, RB_RotatePic, RB_RotatePic2]`, `rendererSpStyle2DProjection=true`, `rendererSpTexturePolicy=true`, `rendererSpScreenUploadPath=true`, `soundSpAttenuationPolicy=true`, `inputSpEarlyDeviceInit=true`, `uiPolicy=mandated-shared-ef-sp-code-ui-owns-behavior`, zero packaged/staged UI scripts, zero loose runtime original images, `legacyStringCount=0`, and DDS formats `DXT1=3664`, `BGRA32=947`, `RGB565=7`.
- No CXBX-R runtime proof is claimed for this checkpoint yet. Next runtime proof should use the dedicated capture tooling at `C:\Games\Emulators\CXBX-CodexCapture` after reading that folder's `AGENTS.md`.
