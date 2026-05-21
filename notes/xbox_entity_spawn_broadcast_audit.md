# Xbox SP entity spawn/broadcast audit

Maps scanned: 34 SP map entity lumps. Entities: 31888. Classnames: 146.

High-risk classes: 7. Medium-risk classes: 12. Missing non-world spawn handlers: 0.

## High-risk visual/server-to-client candidates

| classname | count | maps | spawn | why | samples |
|---|---:|---:|---|---|---|
| func_wall | 111 | 17 | SP_func_wall | inline bmodel starts/uses NOCLIENT or INACTIVE | hoth3:*48:tn=jadenplat / hoth3:*49:tn=aloraplat / kor2:*36:tn=bridge_clip / kor2:*37:tn=pillar_clip_init / kor2:*60:tn=rocks_clip / kor2:*63:tn=block_fall |
| func_glass | 80 | 6 | SP_func_glass | inline mover without explicit broadcast in spawn body | t1_fatal:*128 / t1_fatal:*129 / t1_rail:*115 / t1_rail:*116 / t1_rail:*135 / t1_rail:*136 |
| trigger_push | 25 | 5 | SP_trigger_push | inline bmodel starts/uses NOCLIENT or INACTIVE; normally non-rendered logic entity | t1_fatal:*188:tn=ddm_push / t3_hevil:*50 / t3_hevil:*51 / t3_hevil:*52 / t3_hevil:*53 / t3_hevil:*54 |
| func_train | 14 | 4 | SP_func_train | inline mover without explicit broadcast in spawn body | t1_fatal:*2:tn=Camorigin_Bomb2 / t1_fatal:*159:tn=cam_train_a / t1_fatal:*160:tn=t7664 / t1_fatal:*161:tn=t7680 / t1_fatal:*162:tn=t7697 / t2_rancor:*59:tn=t1111118253 |
| trigger_location | 13 | 3 | SP_trigger_location | inline bmodel starts/uses NOCLIENT or INACTIVE; normally non-rendered logic entity | t3_stamp:*52 / t3_stamp:*53 / t3_stamp:*89 / t3_stamp:*90 / t3_stamp:*91 / t3_stamp:*92 |
| func_rotating | 9 | 2 | SP_func_rotating | inline mover without explicit broadcast in spawn body | t2_rancor:*43 / t3_hevil:*122 / t3_hevil:*134:tn=remove_fan / t3_hevil:*136:tn=remove_fan / t3_hevil:*142 / t3_hevil:*154 |
| func_plat | 8 | 5 | SP_func_plat | inline mover without explicit broadcast in spawn body | t2_dpred:*238 / t2_dpred:*286 / t2_rancor:*154 / t3_bounty:*179 / taspir2:*90 / taspir2:*180 |

## Medium-risk visual candidates

| classname | count | maps | spawn | why | samples |
|---|---:|---:|---|---|---|
| misc_model_static | 6205 | 34 | SP_misc_model_static | external visual path not obvious from spawn body | academy1:models/items/datapad.md3 / academy1:models/items/binoculars.md3 / academy1:models/items/remote.md3 / academy1:models/map_objects/nar_shader/cup.md3 / academy1:models/map_objects/nar_shader/cup.md3 / academy1:models/map_objects/nar_shader/cup.md3 |
| func_breakable | 551 | 24 | SP_func_breakable | inline bmodel, has some broadcast path | hoth2:*64:tn=break_glass / hoth2:*76 / hoth3:*82 / hoth3:*83 / hoth3:*84 / kor1:*75:tn=grate2 |
| misc_model_breakable | 490 | 23 | SP_misc_model_breakable | external model may start hidden/inactive | hoth2:models/map_objects/imperial/switch.md3 / hoth2:models/map_objects/imperial/field_post.md3 / hoth2:models/map_objects/imperial/field_post.md3 / t1_danger:models/map_objects/danger/ship_item04_placed.md3:tn=bustedpiece / t1_fatal:models/map_objects/factory/bomb_new_deact.md3 / t1_fatal:models/map_objects/factory/bomb_new_deact.md3 |
| func_door | 459 | 27 | SP_func_door | inline bmodel, has some broadcast path | hoth2:*46 / hoth2:*47 / hoth2:*51 / hoth2:*52 / hoth2:*53 / hoth2:*54 |
| func_usable | 223 | 22 | SP_func_usable | inline bmodel, has some broadcast path | hoth2:*62:tn=AT_AT_forcefield / hoth2:*94:tn=a5 / hoth2:*120:tn=a5 / kor2:*61:tn=DSmid_scepter / kor2:*71:tn=end_scepter / kor2:*72:tn=scepter |
| func_static | 216 | 25 | SP_func_static | inline bmodel, has some broadcast path | hoth2:*66:tn=breakable_glass_button / hoth2:*86 / hoth2:*96 / hoth2:*98 / hoth3:*55 / hoth3:*76 |
| misc_weather_zone | 73 | 5 | SP_misc_weather_zone | inline bmodel relies on normal snapshot/PVS | hoth2:*69 / hoth2:*70 / hoth2:*71 / hoth2:*72 / hoth2:*77 / hoth2:*78 |
| misc_model_ammo_rack | 68 | 13 | SP_misc_model_ammo_rack | external visual path not obvious from spawn body | hoth2:models/map_objects/imperial/weaponsrung.md3 / hoth2:models/map_objects/imperial/weaponsrung.md3 / hoth3:models/map_objects/imperial/weaponsrack.md3 / hoth3:models/map_objects/imperial/weaponsrack.md3 / t1_fatal:models/map_objects/kejim/weaponsrung.md3 / t1_fatal:models/map_objects/imperial/weaponsrung.md3 |
| rail_mover | 57 | 1 | SP_rail_mover | inline bmodel relies on normal snapshot/PVS | t1_rail:*36 / t1_rail:*57 / t1_rail:*58 / t1_rail:*59 / t1_rail:*95 / t1_rail:*96 |
| misc_model_gun_rack | 38 | 10 | SP_misc_model_gun_rack | external visual path not obvious from spawn body | hoth2:models/map_objects/imperial/weaponsrack.md3 / hoth2:models/map_objects/imperial/weaponsrack.md3 / t1_fatal:models/map_objects/imperial/weaponsrack.md3 / t1_fatal:models/map_objects/imperial/weaponsrack.md3 / t1_fatal:models/map_objects/imperial/weaponsrack.md3 / t1_fatal:models/map_objects/imperial/weaponsrack.md3 |
| rail_track | 3 | 1 | SP_rail_track | inline bmodel relies on normal snapshot/PVS | t1_rail:*161:tn=trackRight / t1_rail:*163:tn=mainTrack / t1_rail:*165:tn=trackLeft |
| rail_lane | 2 | 1 | SP_rail_lane | inline bmodel relies on normal snapshot/PVS | t1_rail:*162:tn=mainRight / t1_rail:*164:tn=mainLeft |

## Missing handlers after item fallback

None, other than `worldspawn` being handled outside normal class dispatch.

## Interpretation

This pass did not find unimplemented BSP classnames. The likely failure mode is narrower:

- The missing Taspir2 blockers/doors/debris are known spawn classes, not unknown entities.
- The risky classes are mostly inline brush models that rely on normal server-to-client visibility rules unless a Raven mapper or script explicitly forces broadcast.
- Trigger volumes such as `trigger_push` and `trigger_location` are expected to be `SVF_NOCLIENT` and should not be treated as missing visuals.
- `rail_track` and `rail_lane` also should not be broadcast; they set up rail-system extents and are freed/logical entities.

## Current best suspects

1. `func_door`
   Door visibility regressed when the door-specific Xbox broadcast was removed. Restore/keep the narrow `func_door` broadcast path unless a later visual pass proves a better root fix. Current Taspir2 smoke logs `DOOR_BROADCAST` for spawned doors and still reaches active gameplay.

2. `func_breakable`
   Already patched on Xbox for scripted invincible breakables with a `targetname`. This explained the Taspir2 blockers/debris appearing.

3. `func_glass`
   Inline `ET_MOVER`, `SVF_GLASS_BRUSH | SVF_BBRUSH`, no explicit broadcast in stock code. Worth a narrow test later, but not enough to explain Taspir2 closed doors.

4. `func_plat`, `func_train`, `func_rotating`
   Inline movers with `InitMover` and no explicit broadcast. These are low-count but visually meaningful. Test one class at a time if future BSP symptoms point at them.

5. `func_wall`
   Conditional inline wall. Needs care: start-off walls intentionally use `SVF_NOCLIENT`/`EF_NODRAW`; only visible/on walls should be considered for broadcast.

6. `func_static`
   Taspir2 contains many script-addressed static brush props (`ore_*`, `clay*`, `side_mash*`, `top_mash*`, belts). Current Xbox test hook broadcasts/logs only targetnamed/script-targetnamed statics, not every static brush.

7. `misc_model_breakable`
   Taspir2 thrown crate/object props are script-addressed external models. Current Xbox test hook broadcasts/logs targetnamed/scripted/targeted breakable models.

8. `rail_mover`
   Starts as `EF_NODRAW` and becomes visible from rail-system code. Only relevant to `t1_rail`.

## Current Taspir2 probe result

Latest CXBX-R smoke reached 75 active seconds without fatal errors or texture allocation failures. The Taspir2 spawn pass logged:

- `DOOR_BROADCAST`: spawned `func_door` entities. This was restored after manual testing caught a missing-door regression.
- `MOVER_BROADCAST`: scripted/on `func_wall` and scripted `func_static`.
- `FUNC_USABLE_BROADCAST`: `t440`, `run_tauntingAlora3`, `ore_1`, `force_field`, `mff1`, `mff2`.
- `MISC_MODEL_BREAKABLE_BROADCAST`: `thrownCrate1`, `thrownCrate2`, `thrownCrate3`, `thrownObject1`.
- `BBRUSH_BROADCAST_SCRIPTED_INVINCIBLE`: Taspir2 scripted block/breakable brush set.

This proves the server side is now emitting the missing-object candidate classes. Visual confirmation is still needed for the cutscene/debris spots because the automated smoke does not reliably drive the exact script moments.

Follow-up Taspir2 smoke with the door path removed still passed 75 active seconds. It logged the same scripted/static/breakable/usable candidates and proved `misc_model_breakable` crate entities reach `CG_General` with a valid registered model handle. The remaining observed issue for breakables is therefore more likely render/material/lighting than server transmission.

After the follow-up, the Xbox bmodel vertex-light path was changed from replacing baked vertex lighting with entity ambient to blending by channel and enforcing a conservative minimum floor of 64 for non-world brush models. A retry smoke passed 75 active seconds after one CXBX cooldown. The new trace showed 32 `BMODEL_VERTEX_LIGHT_BLEND` samples with `floor=64,64,64`; prior near-black samples such as baked `10,10,10` and ambient `35,35,35` now output `64,64,64`. This is a polish-oriented breakable/static-brush shading improvement; do not spend more time here until the structural visibility/depth/projectile issues are handled.

## Current door/HOM/projectile probe

Door regression fix:

- `func_door` spawn broadcast was restored in `SP_func_door`.
- Doors are rendered through `CG_Mover`, not `CG_General`; the active fix is the `CG_MOVER_DOOR_NOCULL` path.
- The renderer now honors `RF_XBOX_NOCULL_BMODEL` for door brush models that local box culling incorrectly rejects.
- Taspir2 smoke logs show `CG_MOVER_DOOR_NOCULL` and `R_BMODEL_FORCE_NOCULL` for the key Taspir2 door entities (`622`, `623`, plus later doors such as `677`, `745`, `793`).

This means the HOM masking fix is no longer a broad world draw hack; it is currently a narrow door/mover cull override. Manual visual validation is still required because automated smoke cannot inspect the doorway.

Scripted projectile fix:

- The missing cinematic bolt was created by `misc_weapon_shooter`.
- Before the fix, the missile was rejected from the client snapshot because it was in BSP area `6` while the view/client was in area `1`, then it impacted before the client ever received it.
- `CreateMissile` now marks only missiles owned by `misc_weapon_shooter` with `SVF_BROADCAST`.
- After the fix, the same Taspir2 smoke logs:
  - `SV_AddEntToSnapshot missile add ent=195 weapon=2`
  - `CG_Missile #0 ent=195 weapon=2`
  - `FX_BryarProjectileThink ... effect=73`
  - `CG_EntityEvent weaponish ... event=55`
  - `FX_BryarHitWall effect=76`

So the scripted shot now reaches the client and requests both projectile and impact FX. If the bolt is still invisible visually, the next likely target is the FX scheduler/render primitive path, not server snapshot delivery.

## Experimental note

A broad spawn-audit hook and a broad inline-mover broadcast experiment were tried and then backed out. CXBX-R became stuck before `CA_ACTIVE` during weapon registration, before the new entity-path logs could prove anything useful. Keep future runtime probes narrow and class-specific; the static audit is the safer guide for coverage.

## Later visual/audio QA flags

These came from manual testing and are not specific to one map:

- Lighting appears missing or heavily reduced.
- Character mouths do not animate; likely tied to dialogue/audio/lip-sync playback.
- Character shadows render as square planes and have floor z-ordering problems.
- Textures look muddy; audit DDS mip selection, `r_picmip`, and the current Xbox DDS top-mip skipping before changing this.
