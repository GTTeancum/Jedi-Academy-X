# Static Constructor Crash Analysis — Jedi Academy Xbox Port

**Date:** 2026-04-21  
**Build:** SP XBE (`code/x_exe/x_exe.vcproj` + `code/x_game/x_game.vcproj`)  
**Compiler:** MSVC 7.1 (XDK `C:\XDK\xbox\bin\vc71\CL.Exe`)  
**Defines:** `NDEBUG;_XBOX;_JK2EXE;WIN32;VV_LIGHTING` (x_exe); `NDEBUG;_XBOX;_LIB;WIN32;GAME_HARD_LINKED` (x_game)

---

## Executive Summary

The game crashes before `main()` (before `WinMainCRTStartup` fully completes its CRT init sweep) because file-scope C++ objects with `std::map` and `std::list` members have their constructors called by `_cinit`/`__initterm`. These constructors call `operator new` to allocate sentinel nodes. The global `operator new` override in `win_main_console.cpp` routes to `Z_Malloc(size, TAG_NEWDEL, qfalse)`. Although `Z_Malloc` now has a self-init guard (`if (!s_Initialized) Com_InitZoneMemory()`), that call chain pulls in `Com_InitZoneMemory()` which calls `gTextures.Initialize()` and `G_ReserveZoneGentities()` — both of which require the full D3D device and game subsystem to be ready. That is not the case during `_cinit`.

**Confirmed root cause:** `std::map` and `std::list` default constructors allocate a sentinel node via `operator new`, which triggers zone init before the system is ready to handle it.

---

## 1. Complete List of File-Scope Globals With Non-Trivial Constructors

### 1.1 CONFIRMED PROBLEMATIC — allocate via operator new during _cinit

#### A. `g_ThaiCodes` — `renderer/tr_font.cpp` line 575
```cpp
ThaiCodes_t g_ThaiCodes;
```
**Object definition** (`tr_font.cpp` lines 100–116):
```cpp
struct ThaiCodes_t {
    map<int, int>   m_mapValidCodes;       // <-- ALLOCATES SENTINEL NODE
    vector<int>     m_viGlyphWidths;       // safe: no alloc in default ctor
    string          m_strInitFailureReason; // safe: MSVC 7.1 SSO, empty string = no alloc
    ThaiCodes_t() { Clear(); }
    void Clear() {
        m_mapValidCodes.clear();
        m_viGlyphWidths.clear();
        m_strInitFailureReason = "";
    }
};
```

**Why it crashes:** `m_mapValidCodes` is `std::map<int,int>`. The `std::map` default constructor in MSVC 7.1 allocates a header/sentinel node using `operator new` regardless of whether any entries are present. This happens during member construction, before the `ThaiCodes_t()` body runs `Clear()`. `Clear()` then calls `m_mapValidCodes.clear()` which is a no-op on an empty map, but the sentinel is already allocated.

**Linked into XBE?** YES. `renderer/tr_font.cpp` is in `x_exe.vcproj` (line 669). Confirmed in `default.map`:
```
0009:004f5afc  ?g_ThaiCodes@@3UThaiCodes_t@@A  00c34afc  tr_font.obj
```

**Xbox usage:** There is NO `#ifdef _XBOX` guard around `g_ThaiCodes` or `ThaiCodes_t`. The Xbox `tr_font.cpp` uses `GetLanguageEnum()` which always returns `eWestern` (hardcoded stub at line 50–54), so Thai functionality is never actually invoked at runtime. The global still has its constructor called at startup.

**Note on `string m_strInitFailureReason`:** In MSVC 7.1, `std::string`'s default constructor uses Small String Optimization (SSO) with a 16-byte in-object buffer. An empty string does NOT call `operator new`. However, `map` absolutely does.

---

#### B. `g_mapFontIndexes` — `renderer/tr_font.cpp` line 271
```cpp
typedef map<sstring_t, int> FontIndexMap_t;
FontIndexMap_t g_mapFontIndexes;
```
**Why it crashes:** `FontIndexMap_t` is `std::map<sstring_t, int>`. The `sstring_t` key type is a fixed-size char array template with trivial default ctor (no allocation), but the `std::map` itself still allocates a sentinel node during default construction.

**Linked into XBE?** YES. Same TU as g_ThaiCodes. Confirmed in `default.map`:
```
0009:004f5ae0  ?g_mapFontIndexes@@3V?$map@...  00c34ae0  tr_font.obj
```

---

#### C. `g_vFontArray` — `renderer/tr_font.cpp` line 269
```cpp
vector<CFontInfo *> g_vFontArray;
```
**Why it is (probably) safe:** `std::vector` default constructor in MSVC 7.1 sets `_Myfirst = _Mylast = _Myend = NULL`. It does NOT allocate heap memory until the first `push_back`/`resize`. Zero allocation during `_cinit`.

**Verdict:** Likely safe, but note it IS a file-scope STL container that has a non-trivial destructor (will call `operator delete` on the internal buffer at program exit).

---

#### D. `theFxScheduler` — `cgame/FxScheduler.cpp` line 22
```cpp
CFxScheduler theFxScheduler;
```
**CFxScheduler class members** (`FxScheduler.h` lines 429–438):
```cpp
typedef map<fxString_t, int>       TEffectID;
typedef list<SScheduledEffect*>    TScheduledEffect;

SEffectTemplate   mEffectTemplates[FX_MAX_EFFECTS];  // plain array, no STL
TEffectID         mEffectIDs;     // <-- std::map: ALLOCATES SENTINEL
TScheduledEffect  mFxSchedule;   // <-- std::list: ALLOCATES SENTINEL
```
**Constructor** (`FxScheduler.cpp` lines 153–157):
```cpp
CFxScheduler::CFxScheduler() {
    memset(&mEffectTemplates, 0, sizeof(mEffectTemplates));
    memset(&mLoopedEffectArray, 0, sizeof(mLoopedEffectArray));
}
```
**Why it crashes:** Although the constructor body only does memsets, the C++ standard mandates that member subobject constructors run BEFORE the constructor body. `mEffectIDs` (a `std::map`) and `mFxSchedule` (a `std::list`) have their default constructors called first. Both allocate sentinel nodes via `operator new`.

**Linked into XBE?** YES. `FxScheduler.cpp` is in `x_game.vcproj` (line 936). `x_game.lib` is linked into the XBE. Confirmed in `default.map`:
```
0009:0090fbf0  ?theFxScheduler@@3VCFxScheduler@@A  0104ebf0  x_game:FxScheduler.obj
```

---

#### E. `g_vstrEffectsNeededPerSlot` — `cgame/FxScheduler.cpp` line 26
```cpp
vector<sstring_t> g_vstrEffectsNeededPerSlot;
```
**Why it is (probably) safe:** `std::vector<sstring_t>` default ctor sets internal pointers to NULL, no heap allocation. Safe during `_cinit`.

**Verdict:** Likely safe for the same reason as `g_vFontArray`.

---

### 1.2 FILE-SCOPE GLOBALS WITH NON-TRIVIAL CONSTRUCTORS BUT NO HEAP ALLOCATION

These are confirmed at file scope, have user-defined constructors, but do NOT call `operator new`:

| Symbol | File | Type | Constructor Notes |
|--------|------|------|-------------------|
| `gTextures` | `win32/win_qgl_dx8.cpp:54` | `StaticTextureAllocator` | Empty constructor `{}`. Safe. |
| `mOutside` | `renderer/tr_WorldEffects.cpp:897` | `COutside` | Constructor calls `Reset()` which sets primitives/flags and calls `ratl::vector_vs::clear()` (no heap). Safe. |
| `mWindZones` | `renderer/tr_WorldEffects.cpp:388` | `ratl::vector_vs<CWindZone, N>` | ratl vector ctor just sets `mSize=0`. No heap. Safe. |
| `mLocalWindZones` | `renderer/tr_WorldEffects.cpp:389` | `ratl::vector_vs<CWindZone*, N>` | Same. Safe. |
| `mParticleClouds` | `renderer/tr_WorldEffects.cpp:1719` | `ratl::vector_vs<CParticleCloud, N>` | Same. Safe. |
| `Settings` | `qcommon/xb_settings.cpp:13` | `XBSettings` | Sets primitive members only. No STL. Safe. |
| `theFxHelper` | `cgame/FxUtil.cpp:22` | `SFxHelper` | POD struct, no constructor defined. Zero-init. Safe. |
| `_padInfo` | `win32/win_input_console.cpp:40` | `PadInfo` | Plain struct, no constructor. Safe. |

---

### 1.3 FUNCTION-LOCAL STATICS (LAZY-INIT — safe at _cinit time)

MSVC 7.1 initializes function-local statics on first call, NOT during `_cinit`. These are safe:

| Symbol | File/Location | Notes |
|--------|---------------|-------|
| `Ghoul2InfoArray singleton` | `ghoul2/G2_API.cpp:533` inside `TheGhoul2InfoArray()` | `Ghoul2InfoArray` ctor calls `std::list::push_back()` in a loop, but this runs only on first `TheGhoul2InfoArray()` call, well after zone init. |
| `CPool thePool` | `qcommon/hstring.cpp:454` inside `ThePool()` | Lazy init. Safe. |
| `static vector<CGhoul2Info> null` | `ghoul2/G2_API.cpp:482` inside `Get()` | Function-local static vector. Safe. |

---

### 1.4 POINTERS AT FILE SCOPE (NULL — trivially safe)

| Symbol | File | Notes |
|--------|------|-------|
| `CIcarus** CIcarus::s_instances = NULL` | `icarus/IcarusImplementation.cpp:67` | Raw pointer, NULL. No ctor. |
| `ShaderEntryPtrs_t* ShaderEntryPtrs` | `renderer/tr_stl.cpp:28` | Pointer to map, NULL. No ctor. Created via `new` on demand. |
| `static vector<boneInfo_t*>* rag = NULL` | `ghoul2/G2_bones.cpp:1118` | Pointer to vector, NULL. Safe. |
| `list<sstring_t>* strList = NULL` | `game/g_savegame.cpp:139` | Local in function. Not file-scope. |

---

### 1.5 GLOBALS BEHIND `#ifdef _G2_GORE` — NOT compiled for Xbox

The following maps exist in `ghoul2/G2_misc.cpp` and `cgame/G2_misc.cpp` but are wrapped in `#ifdef _G2_GORE`. Since `_G2_GORE` is NOT defined in either `x_exe.vcproj` or `x_game.vcproj`, these do NOT appear in the XBE:

```cpp
static map<int,GoreTextureCoordinates> GoreRecords;       // _G2_GORE only
static map<pair<int,int>,int> GoreTagsTemp;               // _G2_GORE only
static map<int,CGoreSet*> GoreSets;                       // _G2_GORE only
```

---

## 2. Linkage Verification — Which Globals Are in the SP XBE

From the linker map `code/x_exe/Release/default.map`:

| Symbol | Map Entry | Source |
|--------|-----------|--------|
| `g_ThaiCodes` | `0009:004f5afc` | `tr_font.obj` |
| `g_mapFontIndexes` | `0009:004f5ae0` | `tr_font.obj` |
| `g_vFontArray` | `0009:004f5aec` | `tr_font.obj` |
| `theFxScheduler` | `0009:0090fbf0` | `x_game:FxScheduler.obj` |
| `g_vstrEffectsNeededPerSlot` | `0009:0090fbdc` | `x_game:FxScheduler.obj` |

The `.CRT$XCU` section in the map shows `0x100` bytes (64 slots for static ctor function pointers). `ThaiCodes_t::ThaiCodes_t` (`??0ThaiCodes_t@@QAE@XZ`) and `CFxScheduler::CFxScheduler` (`??0CFxScheduler@@QAE@XZ`) are confirmed linked and will be called during `_cinit`.

The cgame `tr_font.cpp` (at `code/cgame/tr_font.cpp`) is NOT linked into x_exe directly (it only appears in x_game.vcproj context, but checking x_game.vcproj shows it is also absent from there). Only `renderer/tr_font.cpp` is linked via x_exe.vcproj.

---

## 3. ThaiCodes_t Deep Dive

### 3.1 Members Requiring Allocation

```cpp
struct ThaiCodes_t {
    map<int, int>  m_mapValidCodes;       // MSVC 7.1 std::map: allocates sentinel in ctor
    vector<int>    m_viGlyphWidths;       // MSVC 7.1 std::vector: no alloc in default ctor
    string         m_strInitFailureReason; // MSVC 7.1 std::string with SSO: no alloc for empty
};
```

### 3.2 The Constructor

```cpp
ThaiCodes_t() { Clear(); }
void Clear() {
    m_mapValidCodes.clear();       // no-op on newly constructed map
    m_viGlyphWidths.clear();       // no-op on newly constructed vector
    m_strInitFailureReason = "";   // assign empty string, in-place via SSO
}
```

The `Clear()` body does not cause additional allocations. The damage is done during implicit member construction of `m_mapValidCodes`.

### 3.3 Xbox Usage

`GetLanguageEnum()` in the Xbox build always returns `eWestern` (the function is a hardcoded stub). The `g_ThaiCodes.Init()` call at `tr_font.cpp:1038` is guarded by a condition checking `Language_IsThai()`, which will never return true on Xbox. **Thai font functionality is dead code on Xbox.** The global object serves no purpose.

### 3.4 `Clear()` at Shutdown

`R_ShutdownFonts()` calls `g_ThaiCodes.Clear()` at `tr_font.cpp:1733`. `Clear()` calls `m_mapValidCodes.clear()` which frees sentinel-only structure, `m_viGlyphWidths.clear()`, and assigns `m_strInitFailureReason = ""`. No issues at shutdown (zone is initialized by then).

### 3.5 What `std::string` Does in MSVC 7.1

MSVC 7.1's `std::string` uses the "short string optimization" with a 16-byte union: strings of <=15 chars are stored in an in-object `char _Buf[16]` field. The default constructor sets `_Myres = 15` and `_Mysize = 0`, zeroes `_Buf[0]`. **No heap allocation.** This member alone would be safe.

---

## 4. CFxScheduler and SFxHelper

### 4.1 CFxScheduler

**File:** `cgame/FxScheduler.cpp`, compiled into `x_game.lib`, linked into the SP XBE.

**Problematic members:**
- `TEffectID mEffectIDs` — `std::map<sstring_t, int>` — default ctor allocates sentinel
- `TScheduledEffect mFxSchedule` — `std::list<SScheduledEffect*>` — default ctor allocates sentinel

**Constructor body** (lines 153–157) does only `memset` on OTHER members (`mEffectTemplates`, `mLoopedEffectArray`). The STL member ctors run first, before the body, making the memsets irrelevant to the sentinel allocation.

**Does CFxScheduler::CFxScheduler allocate?** YES. Both `mEffectIDs` (map) and `mFxSchedule` (list) call `operator new` to allocate their internal sentinel/header nodes.

### 4.2 SFxHelper

**File:** `cgame/FxUtil.cpp`, compiled into `x_game.lib`.

```cpp
struct SFxHelper {
    int   mTime;
    int   mFrameTime;
    float mFloatFrameTime;
    // ... only method declarations, no STL members
};
SFxHelper theFxHelper;
```

`SFxHelper` has no user-defined constructor and no non-trivial members. It is trivially zero-initialized as a global. **Safe — no allocation.**

### 4.3 `g_vstrEffectsNeededPerSlot`

```cpp
vector<sstring_t> g_vstrEffectsNeededPerSlot;
```
`std::vector` default ctor does NOT allocate. Sets `_Myfirst = _Mylast = _Myend = 0`. **Safe.**

---

## 5. The operator new Override

### 5.1 Is It the Only Global Override?

YES. Exhaustive search of all `.cpp` files in the codebase finds `operator new` and `operator delete` defined at global scope only in `code/win32/win_main_console.cpp` (lines 31–56). No other file defines these.

```cpp
void *NEWDECL operator new(size_t size) {
    return Z_Malloc(size, TAG_NEWDEL, qfalse);
}
void *NEWDECL operator new[](size_t size) {
    return Z_Malloc(size, TAG_NEWDEL, qfalse);
}
void NEWDECL operator delete(void *ptr) {
    if (ptr) Z_Free(ptr);
}
void NEWDECL operator delete[](void *ptr) {
    if (ptr) Z_Free(ptr);
}
```

### 5.2 What Z_Malloc Does Before Zone Init

`code/qcommon/z_memman_console.cpp:757–762`:
```cpp
void *Z_Malloc(int iSize, memtag_t eTag, qboolean bZeroit, int iAlign) {
    // Zone now initializes on first use. (During static constructors)
    if (!s_Initialized)
        Com_InitZoneMemory();
    ...
    WaitForSingleObject(s_Mutex, INFINITE);
    ...
}
```

**It does NOT crash immediately on uninitialized zone.** Instead, it calls `Com_InitZoneMemory()` which attempts to:
1. Call `Com_Printf("Initialising zone memory...\n")` — calls through uninitialized print subsystem
2. Call `GlobalMemoryStatus(&status)` — Xbox kernel call, generally safe
3. Call `gTextures.Initialize(TEXTURE_POOL_SIZE)` — calls `D3D_AllocContiguousMemory()`. **This requires the D3D device to be initialized**, which it is NOT during `_cinit`. This is the actual crash point on retail hardware.
4. Call `G_ReserveZoneGentities()` — requires game subsystem initialization
5. Call `Cmd_AddCommand(...)` — requires command system initialization
6. Call `CreateMutex(NULL, FALSE, NULL)` — Xbox kernel call; generally safe but the result is stored in `s_Mutex`, and `WaitForSingleObject(s_Mutex, INFINITE)` is called right after `Com_InitZoneMemory()` returns.

**Summary:** Z_Malloc's self-init guard (`if (!s_Initialized) Com_InitZoneMemory()`) was an attempt to handle static ctor calls, but `Com_InitZoneMemory()` itself calls `gTextures.Initialize()` which calls `D3D_AllocContiguousMemory()`. On retail Xbox hardware, contiguous memory allocation via D3D requires the D3D device to be set up, which has not happened during `_cinit`. Result: **crash inside `gTextures.Initialize()`**.

### 5.3 When Is It Safe to Call Z_Malloc?

Zone memory becomes safe after `Com_InitZoneMemory()` completes successfully. In the normal boot path, this is called from `Com_Init()` → `Com_InitZoneMemory()`. The D3D device must already be initialized (done in `WinMain()` before `Com_Init()` is called). Therefore Z_Malloc is safe only after `WinMain()` has initialized D3D and called `Com_Init()`.

---

## 6. Fix Option Evaluation

### Option A: Remove the global operator new/delete override

**Approach:** Delete the four override functions from `win_main_console.cpp`. STL containers will use the CRT's `malloc`/`free`.

**Pros:**
- Completely eliminates the _cinit problem. No static ctors can trigger zone allocation.
- Simple one-line change.
- All STL containers used at file scope and function-local scope will use the system heap (Xbox CRT `malloc` which calls `XPhysicalAlloc` or equivalent).

**Risks:**
- All `new`/`delete` from game code (ICARUS sequencers, other C++ objects) will allocate from the CRT heap instead of the zone. Memory will NOT be tracked by the zone tag system.
- The `Z_PushNewDeleteTag`/`Z_PopNewDeleteTag` mechanism for controlling which zone tag new/delete allocations use will no longer work.
- On Xbox, the CRT heap and the zone pool must coexist in physical RAM. This is risky: the zone pool is allocated at startup with `VirtualAlloc` claiming most available RAM. CRT malloc on top of that may fail if there's insufficient physical memory.
- `Z_TagFree(TAG_NEWDEL)` calls will no longer free anything (those objects are in CRT heap, not zone).
- Memory leaks may accumulate undetected since zone stats won't track them.

**Verdict:** HIGH RISK for the overall memory system. Do not do this alone.

---

### Option B: Guard operator new with zone-ready check, fall back to malloc

**Approach:**
```cpp
void *operator new(size_t size) {
    if (s_Initialized)
        return Z_Malloc(size, TAG_NEWDEL, qfalse);
    else
        return malloc(size);
}
void operator delete(void *ptr) {
    if (Z_IsInZone(ptr))
        Z_Free(ptr);
    else
        free(ptr);
}
```

**Problems:**
- Requires `s_Initialized` to be accessible from `win_main_console.cpp` (it's `static` in `z_memman_console.cpp`). Needs an exported query function.
- The `delete` side needs to know which allocator owns the pointer. This requires either a per-block flag or range check (`Z_IsInZone`). No such function currently exists.
- Pointers allocated with `malloc` during static ctors would need to be freed with `free`, not `Z_Free`. If any of the STL containers allocate during static ctors and then allocate more during gameplay (after zone init), the mix of malloc/zone pointers in the same container becomes a minefield.
- For the sentinel node specifically: the sentinel is allocated in default ctor (pre-zone), but later `insert()` calls will use zone (post-zone). The container's allocator would hold a mix of pre-zone and post-zone pointers.

**Verdict:** Fragile and dangerous. Not recommended.

---

### Option C: Fix each offending global one-by-one with lazy init or #ifdef _XBOX

**For `g_ThaiCodes` and `g_mapFontIndexes` in `renderer/tr_font.cpp`:**

Thai is dead code on Xbox. The entire `ThaiCodes_t g_ThaiCodes` and `FontIndexMap_t g_mapFontIndexes` could be guarded:

```cpp
#ifndef _XBOX
ThaiCodes_t g_ThaiCodes;
#endif
// ...
#ifndef _XBOX
FontIndexMap_t g_mapFontIndexes;
#endif
```

But `g_mapFontIndexes` is actively used for font lookup caching on Xbox (it maps font name to font index). It cannot simply be `#ifdef`'d out. It would need to be replaced with a flat array or converted to lazy-init.

**Lazy init alternative for g_mapFontIndexes:**
```cpp
#ifdef _XBOX
static FontIndexMap_t *g_mapFontIndexes = NULL;
// wherever g_mapFontIndexes is used, replace g_mapFontIndexes.X() with
// if (!g_mapFontIndexes) g_mapFontIndexes = new FontIndexMap_t;
// g_mapFontIndexes->X()
#else
FontIndexMap_t g_mapFontIndexes;
#endif
```
This works because by the time `RE_RegisterFont()` is first called, zone is initialized.

**For `theFxScheduler` in `cgame/FxScheduler.cpp`:**

Converting `CFxScheduler theFxScheduler` to a pointer + lazy init is more invasive:
```cpp
#ifdef _XBOX
CFxScheduler *g_pFxScheduler = NULL;
CFxScheduler& theFxScheduler_ref() {
    if (!g_pFxScheduler) g_pFxScheduler = new CFxScheduler;
    return *g_pFxScheduler;
}
#define theFxScheduler theFxScheduler_ref()
#else
CFxScheduler theFxScheduler;
#endif
```
This requires every use of `theFxScheduler` to go through the accessor. Since `theFxScheduler` is referenced in many cgame files as a plain global, a macro replacement could be made to work transparently.

**Pros:** Targeted, doesn't require architecture changes, preserves zone tracking for all game allocations.

**Cons:** Invasive across several files, risk of missing a use site, ongoing maintenance burden for new globals.

**Verdict:** Feasible but laborious. For a codebase with only 3 confirmed allocating globals (`m_mapValidCodes`, `mEffectIDs`, `mFxSchedule`), this is the surgical approach.

---

### Option D: Move operator new/delete to a TU that links after static ctor tables

**The claim:** If `operator new` is defined in an obj that links after the CRT init tables, the symbol is "not yet resolved" during static ctor calls.

**This is incorrect.** In a statically-linked executable, all symbol resolution occurs at link time. The `operator new` override is already resolved to the zone allocator before the binary runs. Moving the TU order changes the order of CTORs within the `.CRT$XCU` section, not symbol resolution. The crash happens because zone is uninitialized at ctor time, not because the symbol resolves to the wrong function.

**Verdict:** Does not help. Do not pursue.

---

### Option E: Call minimal zone init before _cinit runs (pre-zone init)

**Approach:** Insert a very early init hook that initializes only the zone pool (without `gTextures.Initialize()` and other subsystem calls) before `_cinit` is called.

On Xbox, the entry point chain is: `WinMainCRTStartup` → `_cinit` → `WinMain`. One option is to patch the CRT stub or add a pre-init TU in section `.CRT$XCB` (which runs before `.CRT$XCU`).

**Required:** A minimal `Z_PreInit()` that does ONLY the `VirtualAlloc` for the pool and sets `s_Initialized = true`, without touching D3D or any subsystem:

```cpp
// z_memman_console.cpp addition:
void Z_PreInit(void)
{
    if (s_Initialized) return;
    // Skip gTextures.Initialize (D3D not ready yet)
    // Skip G_ReserveZoneGentities (game not ready yet)
    // Skip Cmd_AddCommand (cmd system not ready yet)
    // Skip CreateMutex if on Xbox (not needed until multithread)
    
    MEMORYSTATUS status;
    GlobalMemoryStatus(&status);
    SIZE_T size = status.dwAvailPhys - ZONE_HEAP_FREE;
    if (size > 64 * 1024 * 1024) size = 64 * 1024 * 1024;
    s_PoolBase = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    s_PoolSize = size;
    // ... setup free block structures ...
    s_Initialized = true;
    // NOTE: gTextures and G_ReserveZoneGentities must still be called
    //       from Com_InitZoneMemory() later, after D3D init.
}
```

Then in `Com_InitZoneMemory()`, guard the early fields:
```cpp
void Com_InitZoneMemory(void) {
    if (s_Initialized) {
        // Pre-init already ran. Now do the D3D-dependent parts:
        gTextures.Initialize(TEXTURE_POOL_SIZE);
        G_ReserveZoneGentities();
        // ... add commands, create mutex ...
        return;
    }
    // Full init path ...
}
```

Hook `Z_PreInit` into `.CRT$XCB` (before XCU):
```cpp
// pre_zone_init.cpp
#pragma section(".CRT$XCB", read)
static void Z_PreInit(void);
__declspec(allocate(".CRT$XCB")) void (*fp_pre_zone_init)(void) = Z_PreInit;
```

**Pros:** Allows zone allocation during static ctors. Preserves zone tracking. Minimal changes.

**Cons:** Defers texture pool and G_ReserveZoneGentities to a second call. Requires careful coordination: `Com_InitZoneMemory()` must NOT call `gTextures.Initialize()` again if already initialized. The `s_Mutex` mutex creation must also be deferred (though `s_Mutex = INVALID_HANDLE_VALUE` is the initial value, and `WaitForSingleObject(INVALID_HANDLE_VALUE, ...)` will behave unexpectedly).

**Critical issue with mutex:** `s_Mutex` is `INVALID_HANDLE_VALUE` until `CreateMutex` is called in `Com_InitZoneMemory`. Z_Malloc calls `WaitForSingleObject(s_Mutex, INFINITE)` AFTER the pre-init guard. If `s_Mutex` is still `INVALID_HANDLE_VALUE` when a static ctor calls `Z_Malloc`, the `WaitForSingleObject` call will fail. Fix: skip the mutex lock during static ctor time (single-threaded), or initialize the mutex in `Z_PreInit`.

**Verdict:** Viable if carefully implemented, but has several subtle traps. The mutex issue must be resolved.

---

### Option F: Replace std::map in ThaiCodes_t with non-allocating alternative

**For `ThaiCodes_t::m_mapValidCodes`:** Replace `std::map<int,int>` with a `ratl::map_vs` or a sorted flat array. `ratl::map_vs` is a fixed-capacity map backed by a sorted array — no heap allocation.

```cpp
#include "../ratl/map_vs.h"
struct ThaiCodes_t {
#ifdef _XBOX
    ratl::map_vs<int, int, MAX_THAI_CODES>  m_mapValidCodes; // no heap
#else
    map<int, int>                            m_mapValidCodes; // original
#endif
    vector<int>  m_viGlyphWidths;
    string       m_strInitFailureReason;
    // ...
};
```

This requires knowing `MAX_THAI_CODES` at compile time. Looking at Thai code loading in `ThaiCodes_t::Init()`, the number of entries equals `iBytesRead / sizeof(int)` from the `tha_codes.dat` file. On Xbox with `GetLanguageEnum()` always returning `eWestern`, Thai code is never initialized, so a zero-capacity placeholder would suffice. But `ratl::map_vs` needs a fixed capacity.

Since Thai is dead on Xbox, the simplest fix for `ThaiCodes_t` is:
```cpp
ThaiCodes_t g_ThaiCodes;  // The clear fix: remove the global, replace with pointer
// becomes:
#ifdef _XBOX
static ThaiCodes_t *g_pThaiCodes = NULL; // never used on Xbox
#define g_ThaiCodes (*g_pThaiCodes)       // will crash if accidentally used
#else
ThaiCodes_t g_ThaiCodes;
#endif
```

**For `g_mapFontIndexes`:** Replace with `ratl::map_vs<sstring_t, int, MAX_FONTS>`:
```cpp
#ifdef _XBOX
#include "../ratl/map_vs.h"
typedef ratl::map_vs<sstring_t, int, 32> FontIndexMap_t; // fixed cap, no heap
#else
typedef map<sstring_t, int> FontIndexMap_t;
#endif
FontIndexMap_t g_mapFontIndexes;
```
`ratl::map_vs<>` uses an in-object sorted array; its default constructor just sets `mSize = 0`. No `operator new` called.

**For `CFxScheduler`:** Replace `std::map` and `std::list` members:
```cpp
#ifdef _XBOX
typedef ratl::map_vs<fxString_t, int, FX_MAX_EFFECTS>    TEffectID;
typedef ratl::list_vs<SScheduledEffect*, MAX_SCHEDULED>   TScheduledEffect;
#else
typedef map<fxString_t, int>          TEffectID;
typedef list<SScheduledEffect*>       TScheduledEffect;
#endif
```

**Pros:** Zero heap during `_cinit`. No changes to zone allocator or boot sequence. Clean fix.

**Cons:** `ratl::map_vs` and `ratl::list_vs` are fixed-capacity. The fixed size must be chosen conservatively. Behavior changes slightly (capacity exceeded = assert/corruption instead of silent growth). The ratl containers are already used elsewhere in the codebase (nav, AI, game logic), so this is consistent.

**Verdict:** This is the cleanest, most durable solution for the specific offending types.

---

## 7. Zone Memory Deep Dive

### 7.1 `s_Initialized` Flag and Z_Malloc Self-Init

```cpp
static bool s_Initialized = false;  // z_memman_console.cpp:186

void *Z_Malloc(int iSize, memtag_t eTag, qboolean bZeroit, int iAlign) {
    if (!s_Initialized)
        Com_InitZoneMemory();       // Self-init attempt
    ...
    WaitForSingleObject(s_Mutex, INFINITE);  // Uses s_Mutex after init
    ...
}
```

The self-init guard was intended to handle static ctor calls. However, `Com_InitZoneMemory()` requires D3D (for `gTextures.Initialize`), the command system (for `Cmd_AddCommand`), and game code (`G_ReserveZoneGentities`). None are available during `_cinit`.

### 7.2 `Z_IsInZone` / Zone Membership Query

No `Z_IsInZone()` function exists. Zone memory occupies a contiguous VirtualAlloc block (`s_PoolBase` to `s_PoolBase + s_PoolSize`). A membership check would need pointer arithmetic:
```cpp
bool Z_IsInZone(void *ptr) {
    return s_Initialized &&
           (char*)ptr >= (char*)s_PoolBase &&
           (char*)ptr < (char*)s_PoolBase + s_PoolSize;
}
```

### 7.3 When Is It Safe to Call Z_Malloc?

`Z_Malloc` is safe only after both:
1. `VirtualAlloc` has allocated the zone pool.
2. `gTextures.Initialize()` has allocated the texture pool (requires D3D device).
3. `CreateMutex` has created `s_Mutex`.

In the normal boot sequence, this happens during `Com_InitZoneMemory()`, called from `Com_Init()`, called from `WinMain()`. `WinMain()` is called after `_cinit` completes. Therefore Z_Malloc cannot safely serve `_cinit`-time allocations without pre-initialization.

---

## 8. ICARUS Class Instantiation at File Scope

ICARUS source files (`icarus/IcarusImplementation.cpp`, `Sequencer.cpp`, `Sequence.cpp`, `TaskManager.cpp`, `BlockStream.cpp`) do NOT define any file-scope class instances with non-trivial constructors.

File-scope data in ICARUS:
```cpp
double CIcarus::ICARUS_VERSION = 1.40;   // primitive, trivial
int    CIcarus::s_flavorsAvailable = 0;  // primitive, trivial
CIcarus** CIcarus::s_instances = NULL;   // pointer, NULL, trivial
```

CIcarus instances are created in `IIcarusInterface::GetIcarus()` via `new CIcarus(index)` — only when first called by game code, well after boot. `CIcarus::CIcarus(int)` constructor initializes STL member containers (`m_sequences`, `m_sequencers`, `m_sequencerMap`, `m_signals`) but this runs post-zone-init. **No problem at _cinit time.**

---

## 9. Summary of Confirmed Allocating Globals

| Global | File | Type | Allocating Member | Linked? | Fix |
|--------|------|------|-------------------|---------|-----|
| `g_ThaiCodes` | `renderer/tr_font.cpp:575` | `ThaiCodes_t` | `map<int,int> m_mapValidCodes` | YES | `#ifdef _XBOX` guard or remove; Thai is dead on Xbox |
| `g_mapFontIndexes` | `renderer/tr_font.cpp:271` | `map<sstring_t,int>` | the map itself | YES | Replace with `ratl::map_vs` or lazy-init pointer |
| `theFxScheduler` | `cgame/FxScheduler.cpp:22` | `CFxScheduler` | `map mEffectIDs`, `list mFxSchedule` | YES (via x_game.lib) | Replace members with `ratl::map_vs`/`ratl::list_vs` |

---

## 10. Recommended Fix

### Tier 1 (Correct, Minimal, Durable): Replace allocating STL members with ratl alternatives

This avoids all static-ctor heap allocation without touching the zone allocator, operator new, or boot sequence.

#### Step 1: `g_ThaiCodes` — Kill it on Xbox (Thai is dead code)

In `renderer/tr_font.cpp`, add `#ifdef _XBOX` guard:
```cpp
#ifndef _XBOX
ThaiCodes_t g_ThaiCodes;
#endif
```
Guard all uses of `g_ThaiCodes` in `tr_font.cpp` with `#ifndef _XBOX`. There are 4 use sites (lines 620, 654, 1038–1041, 1203, 1733). Since `GetLanguageEnum()` always returns `eWestern` on Xbox and the Thai path is never entered, removing the object also simplifies the code.

#### Step 2: `g_mapFontIndexes` — Replace std::map with ratl::map_vs

```cpp
// renderer/tr_font.cpp
#ifdef _XBOX
#include "../ratl/map_vs.h"
typedef ratl::map_vs<sstring_t, int, 32> FontIndexMap_t; // 32 fonts max, no heap
#else
typedef map<sstring_t, int>               FontIndexMap_t;
#endif
FontIndexMap_t g_mapFontIndexes;
```

The `ratl::map_vs` default constructor sets `mSize=0` with no heap allocation. It uses a sorted in-object array. Font lookups are identical in behavior (sorted map semantics). The capacity of 32 is generous; the game loads at most ~10 fonts.

#### Step 3: `CFxScheduler` members — Replace std::map and std::list

In `cgame/FxScheduler.h`:
```cpp
#ifdef _XBOX
#include "../ratl/map_vs.h"
#include "../ratl/list_vs.h"
typedef ratl::map_vs<fxString_t, int, FX_MAX_EFFECTS>       TEffectID;
typedef ratl::list_vs<SScheduledEffect*, MAX_FX_SCHEDULED>   TScheduledEffect;
#else
typedef map<fxString_t, int>         TEffectID;
typedef list<SScheduledEffect*>      TScheduledEffect;
#endif
```
Where `MAX_FX_SCHEDULED` must be chosen to cover the maximum number of simultaneously scheduled effects. A value of 512–1024 is reasonable. The ratl containers are already in the x_game.vcproj.

### Tier 2 (Belt-and-suspenders): Add a pre-init mutex fix

Even after fixing the known globals, future developers may add new allocating globals. A defensive measure is to make Z_Malloc's mutex handling safe before zone init:

```cpp
void *Z_Malloc(int iSize, memtag_t eTag, qboolean bZeroit, int iAlign) {
    if (!s_Initialized)
        Com_InitZoneMemory();

    // ... size/tag processing ...

#ifndef _GAMECUBE
    if (s_Mutex != INVALID_HANDLE_VALUE)
        WaitForSingleObject(s_Mutex, INFINITE);
#endif
    // ... rest of allocation ...
#ifndef _GAMECUBE
    if (s_Mutex != INVALID_HANDLE_VALUE)
        ReleaseMutex(s_Mutex);
#endif
}
```

This makes Z_Malloc safe if called before `CreateMutex` (single-threaded context during `_cinit`). This is a low-risk change.

### Tier 3 (Diagnostic safeguard): Assert in operator new if zone not ready

```cpp
void *NEWDECL operator new(size_t size) {
    if (!s_Initialized) {
        // Log to debug console that pre-zone new was called
        OutputDebugStringA("WARN: operator new called before zone init!\n");
        return malloc(size);  // fallback for debugging
    }
    return Z_Malloc(size, TAG_NEWDEL, qfalse);
}
```

This serves as a diagnostic during development (catches new globals before they hit retail), but as discussed, the malloc/Z_Free mismatch is dangerous for production. Remove before shipping.

---

## 11. Other Globals to Watch — Prevention

These are patterns that could cause similar issues if introduced by future changes:

1. **Any file-scope `std::map`, `std::set`, `std::multimap`, `std::list`** — These ALL allocate sentinel nodes in their default ctors. If added to any TU linked into the XBE, they will crash.

2. **`CMediaHandles` instances at file scope** — `CMediaHandles` contains `vector<int> mMediaList`. Vector is safe (no alloc in default ctor), but any future change to add a map would cause issues.

3. **ICARUS per-class operator new** — ICARUS headers define per-class `operator new` (check `IcarusInterface.h`). ICARUS objects are not instantiated at file scope, so this is currently safe. But if a future change adds a file-scope ICARUS object, it will call zone allocator during `_cinit`.

4. **`Ghoul2InfoArray` singleton** — This is a function-local static (lazy-init) and contains `vector<CGhoul2Info>` and `list<int>`. Currently safe because it's a function-local static. If ever promoted to a file-scope global, it will crash.

5. **`goblib` library** — Not analyzed in depth. The `goblib.vcproj` is also linked. Check it for file-scope STL globals.

---

## 12. References

- Source: `code/win32/win_main_console.cpp:31–56` — global operator new/delete
- Source: `code/qcommon/z_memman_console.cpp:757–840` — Z_Malloc implementation
- Source: `code/qcommon/z_memman_console.cpp:234–344` — Com_InitZoneMemory
- Source: `code/renderer/tr_font.cpp:100–117,269–271,575` — ThaiCodes_t, g_mapFontIndexes, g_vFontArray
- Source: `code/cgame/FxScheduler.cpp:22,26` — theFxScheduler, g_vstrEffectsNeededPerSlot
- Source: `code/cgame/FxScheduler.h:429–438` — CFxScheduler member types (map + list)
- Source: `code/win32/xbox_texture_man.h:23` — StaticTextureAllocator default ctor
- Source: `code/qcommon/sstring.h` — sstring_t (fixed array, no heap)
- Map: `code/x_exe/Release/default.map:27656–27659` — .CRT$XCA/.CRT$XCU/.CRT$XCZ section layout
- Map: `code/x_exe/Release/default.map:28303–28305,29301–29302` — confirmed globals in final XBE
