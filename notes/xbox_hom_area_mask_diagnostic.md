# Xbox HOM / Area-Mask Diagnostic Notes

## Symptom

Taspir2 and Kor2 showed hall-of-mirrors gaps around doorways and portal-like openings. Static world textures were mostly mapped correctly, but some openings rendered as stale/blank framebuffer.

## Diagnostic Change That Fixed HOM Visually

In `code/renderer/tr_world.cpp`, inside the Xbox `R_MarkLeaves(mleaf_s *leafOverride)` path, the area-mask reject was bypassed:

```cpp
if (!lookingForWorstLeaf &&
    leaf->area >= 0 &&
    (tr.refdef.areamask[leaf->area>>3] & (1<<(leaf->area&7)))) {
    areaRejected++;
    // Xbox diagnostic: did not continue here.
}
```

The original behavior counted the area as not visible and continued:

```cpp
continue; // not visible
```

This forced leaves from closed/disconnected areas to still be marked visible if they passed PVS.

## Evidence

Kor2 hidden smoke before this diagnostic:

- Main view marked leaves: `47`
- Main view draw surfaces: `419`
- Area rejected leaves: `190`

Kor2 hidden smoke after bypass:

- Main view marked leaves: `237`
- Main view draw surfaces: `1623`
- Area rejected leaves still counted: `190`
- 180 second hidden smoke passed, no fatal errors.

Taspir2 hidden smoke also passed for 180 seconds. Taspir2 had smaller area-reject counts, usually around `11` to `21` leaves during movement.

## Interpretation

The bypass fixed HOM because it drew through closed area portals. That is probably not the real fix.

The user verified against gameplay footage that the HOM locations should be blocked after the cutscene. In Taspir2, the focused missing objects are not literal `func_door` entities; they are scripted ceiling/debris brush blockers implemented as `func_breakable`. When the area mask works normally, it hides the rooms behind those closed portals; because the blocker objects were missing, the player saw HOM. When the area mask was bypassed, the rooms behind the closed portals drew, masking the missing-object problem.

## Risk

The bypass is too broad for production:

- It can draw areas that should be hidden behind closed doors.
- It inflated Kor2 draw surfaces from `419` to `1623`, so it can hurt performance badly.
- It can mask bugs in mover/brush-model entity rendering, spawning, linking, or snapshot transmission.

## Next Investigation

Focus on missing blocker objects:

- `ET_MOVER` snapshot inclusion.
- Brush-model/inline model registration and rendering.
- `func_door`, `func_wall`, `func_breakable`, `misc_model_breakable`, and scripted debris state.
- Server area portal state changes only after verifying the visual mover/debris entities exist.

Keep these notes because the area bypass is a useful diagnostic if HOM returns later, but it should not remain as the final fix unless no better option exists.

## Follow-Up: Missing Scripted Breakables

The Taspir2 doorway blockers are `func_breakable` brush models, not static world surfaces and not actual `func_door` entities. The focused set was `*129` through `*152`; the missing ceiling/debris blockers around the cutscene doorway included `*139` through `*152`.

Server snapshot diagnostics showed the real failure mode:

- These entities were spawned and linked.
- They were `ET_MOVER` brush models with `SVF_BBRUSH`.
- They were rejected by the server area check because the player was in area `1`, while the blockers were in area `6` or had no second area.
- Before the fix, models `*139` through `*152` had `sent=0` for a full Taspir2 smoke run.

A hard-coded diagnostic broadcast for `*139` through `*152` proved the rest of the pipeline was working:

- Server sent the entities in snapshots.
- Cgame added them as mover entities.
- Renderer added their inline brush models and drawsurfs.
- Taspir2 stayed active with no fatal errors.

The current targeted fix is in `code/game/g_breakable.cpp`: on Xbox, `func_breakable` entities that are both invincible (`spawnflags & 1`) and targetnamed are marked `SVF_BROADCAST` at spawn time. These are script-controlled blockers/debris, and `SVF_NOCLIENT` still wins later if script code hides them.

Validation so far:

- Taspir2: 23 scripted/invincible breakables broadcast; active smoke passed for 180 seconds.
- Taspir2 blockers `*129` through `*152`: received by cgame and rendered as brush models.
- Kor2: 16 scripted/invincible breakables broadcast; active smoke passed for 180 seconds.

This should replace the broad area-mask bypass for scripted breakable blockers. If HOM reappears, first check whether the expected blocker/debris entity was spawned, broadcast, sent in snapshots, and rendered, before considering visibility-area changes again.

Actual `func_door` entities are a separate path in `code/game/g_mover.cpp`. Taspir2 has many real doors, such as models `*126`, `*127`, `*153`, `*167`, `*168`, `*171`, `*172`, `*173`, `*179`, `*181`, `*182`, `*192` through `*197`, `*201`, `*202`, and `*203`. Earlier mover diagnostics showed door models like `*172`, `*193`, `*197`, `*202`, and `*203` being sent normally. If a literal door is missing visually, investigate `func_door` mover state/snapshot/rendering separately instead of widening the `func_breakable` broadcast rule.
