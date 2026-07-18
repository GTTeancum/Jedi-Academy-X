param(
    [Alias("Log")]
    [string]$LogPath,

    [Alias("Xbe")]
    [string]$XbePath
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$candidateLogs = @()

if ($LogPath) {
    $candidateLogs += $LogPath
}

$candidateLogs += @(
    "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\ef_mp_log.txt",
    "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\BaseEF\ef_mp_log.txt",
    "C:\Games\Emulators\CXBX\EmuDisk\Partition1\ef_mp_log.txt",
    "C:\Games\Emulators\CXBX\EmuDisk\Partition2\ef_mp_log.txt",
    "C:\Games\Emulators\CXBX\EmuDisk\Partition3\ef_mp_log.txt",
    (Join-Path $repoRoot "ef_mp_log.txt")
)

$resolvedLog = $candidateLogs |
    Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
    Select-Object -First 1

if (-not $resolvedLog) {
    Write-Host "No ef_mp_log.txt found. Checked:"
    foreach ($candidate in $candidateLogs | Select-Object -Unique) {
        Write-Host "  $candidate"
    }
    exit 2
}

$text = Get-Content -LiteralPath $resolvedLog -Raw
$logInfo = Get-Item -LiteralPath $resolvedLog
$stagedXbe = if ($XbePath) { $XbePath } else { "C:\Games\Emulators\CXBX\Star-Trek-Elite-Force-X\efmp.xbe" }
$logIsStale = $false

if (Test-Path -LiteralPath $stagedXbe -PathType Leaf) {
    $xbeInfo = Get-Item -LiteralPath $stagedXbe
    if ($logInfo.LastWriteTime -lt $xbeInfo.LastWriteTime) {
        $logIsStale = $true
    }
}

$checks = @(
    @{ Name = "Log started"; Pattern = "=== Elite Force Holomatch Xbox log started ===" },
    @{ Name = "efmp runtime marker"; Pattern = "STEFX_HM: efmp\.xbe runtime log sink path='(D:|E:|raw-nt-partition1)" },
    @{ Name = "Shared SP UI VM dispatch"; Pattern = "STEFX_HM: SP EF UI VM dispatch active from shared code/ui" },
    @{ Name = "Uniform SP UI mandate"; Pattern = "STEFX_HM: UI mandate active; uniform SP code/ui owns Holomatch UI" },
    @{ Name = "Uniform SP UI mandate enforced"; Pattern = "STEFX_HM: UI mandate enforced; MP legacy menus stay dead and SP code/ui owns all Holomatch UI behavior" },
    @{ Name = "Shared SP UI no script cache"; Pattern = "STEFX_HM: SP EF UI lifecycle initialized from code/ui; no script menu cache; codemp/ui remains adapter-only" },
    @{ Name = "Shared SP UI skipped legacy renderer font"; Pattern = "STEFX_HM: SP EF UI cache skipped legacy renderer font; EF prop-font atlas owns Holomatch text" },
    @{ Name = "Renderer SP 2D projection"; Pattern = "STEFX_HM: renderer using SP-style top-left 2D projection" },
    @{ Name = "Renderer shader manifest"; Pattern = "STEFX_HM: renderer loaded shader manifest" },
    @{ Name = "Renderer SP screen texture"; Pattern = "STEFX_HM: renderer using SP-style GL_RGBA screen texture; legacy MP GL_LIN_RGBA8 path disabled" },
    @{ Name = "Renderer registration stretch prime skipped"; Pattern = "STEFX_HM: renderer skipped inherited zero-size registration StretchPic prime" },
    @{ Name = "Renderer solid fill reset"; Pattern = "STEFX_HM: renderer forced Xbox solid fill mode where=RB_BeginDrawingView" },
    @{ Name = "Console BaseEF list normalization"; Pattern = "STEFX_HM: Sys_ListFiles normalized BaseEF path" },
    @{ Name = "Console empty strings probe"; Pattern = "STEFX_HM: Sys_ListFiles strings probe empty ext='(dir|str)'" },
    @{ Name = "SV pure loaded PK3 list"; Pattern = "STEFX_HM: SV pure loaded PK3s checksums='[^']+' names='[^']+'" },
    @{ Name = "Input early XInitDevices"; Pattern = "(STEFX_HM: input Plan-B XInitDevices completed before D3D init|STEFX_HM: input using SP early XInitDevices path; gamepad mask=)" },
    @{ Name = "Input SP device path"; Pattern = "STEFX_HM: input using SP early XInitDevices path; gamepad mask=" },
    @{ Name = "Input first gamepad state"; Pattern = "STEFX_HM: first gamepad state port=" },
    @{ Name = "Sound hardened path"; Pattern = "STEFX_HM: sound using EF/SP hardened Xbox path; handle/entity guards active" },
    @{ Name = "QAL effects image downloaded"; Pattern = "STEFX_HM: QAL downloaded Xbox effects image bytes=[1-9][0-9]*" },
    @{ Name = "Sound device opened"; Pattern = "JAMP: S_Init after alcOpenDevice" },
    @{ Name = "Sound registration reached"; Pattern = "(STEFX_HM: sound registration active listeners=|STEFX_HM: CL_StartHunkUsers sound registration done|JAMP: CL_StartHunkUsers S_BeginRegistration skipped for Cxbx smoke testing)" },
    @{ Name = "Startup command"; Pattern = "STEFX_HM: startup command .*fs_game BaseEF" },
    @{ Name = "Direct slice startup flag"; Pattern = "STEFX_HM: startup command .*stefx_hm_directSlice 1" },
    @{ Name = "hm_borg1 direct boot"; Pattern = "(STEFX_HM: direct Holomatch startup bypasses menus; loading hm_borg1 from command line|STEFX_HM: queueing direct Holomatch map)" },
    @{ Name = "hm_borg1 running"; Pattern = "STEFX_HM: direct Holomatch map is running map='hm_borg1'" },
    @{ Name = "Local client active"; Pattern = "STEFX_HM: direct Holomatch local client is active" },
    @{ Name = "EF local model default"; Pattern = "STEFX_HM: client default userinfo model is EF Holomatch model='munro/default'" },
    @{ Name = "Borg bots queued"; Pattern = "STEFX_HM: queueing direct Holomatch (Borg )?bots" },
    @{ Name = "Bot library setup succeeded"; Pattern = "STEFX_HM: BotAISetup trap_BotLibSetup done result=0" },
    @{ Name = "Bot init accepted setup"; Pattern = "STEFX_HM: bot init BotAISetup result=1" },
    @{ Name = "Official EF AAS package probe"; Pattern = "STEFX_HM: official EF AAS package probe map='hm_borg1' file='maps/hm_borg1\.aas' bytes=[1-9][0-9]* checksum='[0-9]+'; generated waypoint route remains (active|available) for Xbox BSP" },
    @{ Name = "Official EF AAS botlib checksum var"; Pattern = "STEFX_HM: official EF AAS botlib checksum var set value='439350207'" },
    @{ Name = "Official EF AAS botlib load"; Pattern = "STEFX_HM: official EF AAS botlib load result=0 map='hm_borg1'" },
    @{ Name = "Fallback waypoint inherited path skipped"; Pattern = "STEFX_HM: fallback bot waypoint inherited CalculatePaths skipped map='hm_borg1' total=[1-9][0-9]*" },
    @{ Name = "Fallback waypoint local links"; Pattern = "STEFX_HM: fallback bot waypoint local links done map='hm_borg1' total=[1-9][0-9]* links=[1-9][0-9]*" },
    @{ Name = "Fallback waypoint trap skipped"; Pattern = "STEFX_HM: fallback bot waypoint trap path calculation skipped map='hm_borg1'; local Holomatch links active" },
    @{ Name = "1_of_12 accepted"; Pattern = "STEFX_HM: addbot accepted name='1_of_12'" },
    @{ Name = "2_of_3 accepted"; Pattern = "STEFX_HM: addbot accepted name='2_of_3'" },
    @{ Name = "Direct combat spawn override"; Pattern = "STEFX_HM: direct Holomatch combat spawn override client=[12]" },
    @{ Name = "Direct combat bot command"; Pattern = "STEFX_HM: direct Holomatch combat bot command client=[12]" },
    @{ Name = "Client active state"; Pattern = "STEFX_HM: ClientBegin active state" },
    @{ Name = "Match heartbeat"; Pattern = "STEFX_HM: match heartbeat .*active=[1-9]" },
    @{ Name = "Match client state"; Pattern = "STEFX_HM: match client state client=" },
    @{ Name = "HUD shaders registered"; Pattern = "STEFX_HM: registered \d+ EF SP interface HUD shaders" },
    @{ Name = "HUD startup armed"; Pattern = "STEFX_HM: EF SP interface HUD startup armed" },
    @{ Name = "HUD startup complete"; Pattern = "STEFX_HM: EF SP interface HUD startup complete" },
    @{ Name = "Cgame UI shim reject-only"; Pattern = "STEFX_HM: cgame UI parser shim is reject-only; shared SP UI owns menus" },
    @{ Name = "Cgame HUD menu disabled"; Pattern = "STEFX_HM: cgame HUD menu system disabled for EF MP; using EF SP interface HUD" },
    @{ Name = "Cgame menu asset cache skipped"; Pattern = "STEFX_HM: cgame skipped inherited menu asset cache for shared SP UI path" },
    @{ Name = "Cgame parser entry points dead"; Pattern = "STEFX_HM: cgame parser entry points are dead; shared SP UI owns menus" },
    @{ Name = "Cgame flag tag probe skipped"; Pattern = "STEFX_HM: cgame skipped inherited flag-carrier tag probe for EF Holomatch players" },
    @{ Name = "HUD legacy bypass route"; Pattern = "STEFX_HM: cgame Holomatch 2D using SP interface-only path; legacy cgame parser HUD bypassed" },
    @{ Name = "Scoreboard parser bypass"; Pattern = "(STEFX_HM: cgame scoreboard uses EF Holomatch overlay; parser scoreboard bypassed|STEFX_HM: cgame skipped ad hoc MP weapon/score HUD text; EF interface and score overlay own status)" },
    @{ Name = "Cgame skipped legacy renderer font"; Pattern = "STEFX_HM: cgame skipped legacy renderer font registration; EF prop-font atlas owns Holomatch text" },
    @{ Name = "EF prop fonts loaded"; Pattern = "STEFX_HM: cgame EF prop fonts loaded=1" },
    @{ Name = "HUD native DDS UV path"; Pattern = "STEFX_HM: cgame EF SP interface HUD using DDS native UV draw path" },
    @{ Name = "HUD draw active"; Pattern = "STEFX_HM: EF SP interface HUD draw active" },
    @{ Name = "Renderer draw/present proof"; Pattern = "(STEFX: fakegl framebuffer sample #[0-9]+ afterPresent=1 .* nonzero=[1-9]|STEFX_HM: renderer draw submission active frame=[0-9]+ surfs=[1-9])" },
    @{ Name = "Renderer present succeeded"; Pattern = "STEFX: fakegl SwapBuffers #[0-9]+ Present hr=0x00000000" },
    @{ Name = "Score request sent"; Pattern = "STEFX_HM: cgame requested Holomatch score snapshot" },
    @{ Name = "Score feed parsed"; Pattern = "STEFX_HM: cgame parsed EF score command" },
    @{ Name = "Score updated"; Pattern = "STEFX_HM: score update client=" },
    @{ Name = "Combat weapon path"; Pattern = "(STEFX_HM: server EF Phaser applied damage attacker=|STEFX_HM: server EF fire bridge weapon=|STEFX_HM: server emitted EF trace impact event weapon=|STEFX_HM: cgame EF missile impact feedback end event=|STEFX_HM: server Holomatch missile impact used EF normal damage weapon=)" },
    @{ Name = "Death scored"; Pattern = "STEFX_HM: player death scored" },
    @{ Name = "Respawn loop"; Pattern = "STEFX_HM: respawn used EF direct path" },
    @{ Name = "MDR loaded"; Pattern = "STEFX_HM: R_LoadMDR loaded" },
    @{ Name = "EF body registered"; Pattern = "STEFX_HM: cgame registered EF player body" },
    @{ Name = "EF body drawn"; Pattern = "STEFX_HM: cgame drew EF player body" }
)

$missing = @()
Write-Host "Holomatch log: $resolvedLog"
Write-Host "Log timestamp: $($logInfo.LastWriteTime)"
if (Test-Path -LiteralPath $stagedXbe -PathType Leaf) {
    Write-Host "Staged efmp.xbe timestamp: $((Get-Item -LiteralPath $stagedXbe).LastWriteTime)"
}
if ($logIsStale) {
    Write-Host "[STALE] Log is older than the staged efmp.xbe; run CXBX-R again before treating missing checkpoints as current."
}

foreach ($check in $checks) {
    if ($text -match $check.Pattern) {
        Write-Host ("[OK]   {0}" -f $check.Name)
    } else {
        Write-Host ("[MISS] {0}" -f $check.Name)
        $missing += $check.Name
    }
}

$forbidden = @(
    @{ Name = "old renderer font DDS miss"; Pattern = "R_LoadImage DDS missing request='fonts/ergoec'" },
    @{ Name = "old flipped HUD DDS UV path"; Pattern = "STEFX_HM: cgame EF SP interface HUD using DDS V-corrected UV draw path" },
    @{ Name = "console list warning"; Pattern = "WARNING: List file .*_console_.*_list_ not found" },
    @{ Name = "old dsstdfx abort"; Pattern = "JAMP: alcOpenDevice missing dsstdfx" },
    @{ Name = "dry audio effects image fallback"; Pattern = "STEFX_HM: QAL effects image sound/dsstdfx\.bin missing; continuing dry audio" },
    @{ Name = "sound device open failed"; Pattern = "JAMP: S_Init alcOpenDevice failed" },
    @{ Name = "false botlib setup failure"; Pattern = "STEFX_HM: BotAISetup trap_BotLibSetup failed" },
    @{ Name = "sv_pure empty PK3 warning"; Pattern = "WARNING: sv_pure set but no PK3 files loaded" },
    @{ Name = "inherited flag tag miss"; Pattern = "STEFX_HM: R_LerpTag MDR missing tag='tag_flag'" },
    @{ Name = "renderer player frame clamp"; Pattern = "STEFX_HM: renderer clamped EF MDR frame" },
    @{ Name = "official EF AAS botlib load failed"; Pattern = "(STEFX_HM: official EF AAS botlib load failed|AAS_LoadMap failed)" },
    @{ Name = "fallback waypoint trap path entered"; Pattern = "STEFX_HM: fallback bot waypoint trap paths begin" },
    @{ Name = "bot alternate-fire primary-only workaround"; Pattern = "STEFX_HM: bot (disabled EF Holomatch alternate fire|converted EF Holomatch alternate fire command to primary fire)" }
)

$forbiddenHits = @()
foreach ($check in $forbidden) {
    if ($text -match $check.Pattern) {
        Write-Host ("[BAD]  {0}" -f $check.Name)
        $forbiddenHits += $check.Name
    } else {
        Write-Host ("[OK]   no {0}" -f $check.Name)
    }
}

Write-Host ""
Write-Host "Last 40 log lines:"
Get-Content -LiteralPath $resolvedLog -Tail 40

if ($missing.Count -gt 0 -or $forbiddenHits.Count -gt 0) {
    Write-Host ""
    if ($missing.Count -gt 0) {
        Write-Host "Missing checkpoints: $($missing -join ', ')"
    }
    if ($forbiddenHits.Count -gt 0) {
        Write-Host "Forbidden checkpoints present: $($forbiddenHits -join ', ')"
    }
    if ($logIsStale) {
        exit 3
    }
    exit 1
}

if ($logIsStale) {
    Write-Host ""
    Write-Host "All checkpoints found, but the log is stale relative to the staged efmp.xbe."
    exit 3
}

Write-Host ""
Write-Host "All Holomatch smoke checkpoints were present."
