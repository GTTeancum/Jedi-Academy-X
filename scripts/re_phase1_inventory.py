"""
re_phase1_inventory.py
Phase 1 XBE inventory: section table, kernel thunks, library versions,
entry point, TLS, and id Tech 3 / Raven string filter.
Writes two output files:
  scripts/output/phase1_inventory.txt  -- full structured dump
  scripts/output/phase1_strings.txt    -- filtered string table
"""

import sys, struct, os, re, io, string
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# ── XBE encryption keys ────────────────────────────────────────────────────
XBE_EP_RETAIL  = 0xA8FC57AB
XBE_EP_DEBUG   = 0x94859D4B
XBE_KT_RETAIL  = 0x5B6D40B6
XBE_KT_DEBUG   = 0xEFB1F152

# ── Xbox kernel ordinal table (selected; covers game-relevant exports) ──────
KRNL = {
    1:  'AvGetSavedDataAddress',
    2:  'AvSendTVEncoderOption',
    3:  'AvSetDisplayMode',
    4:  'AvSetSavedDataAddress',
    5:  'AvSetSavedDataAddress2',
    6:  'DbgBreakPoint',
    7:  'DbgBreakPointWithStatus',
    8:  'DbgLoadImageSymbols',
    9:  'DbgPrint',
    10: 'HalReadSMCTrayState',
    11: 'DbgPrompt',
    12: 'DbgUnLoadImageSymbols',
    13: 'ExAcquireReadWriteLockExclusive',
    14: 'ExAcquireReadWriteLockShared',
    15: 'ExAllocatePool',
    16: 'ExAllocatePoolWithTag',
    17: 'ExEventObjectType',
    18: 'ExFreePool',
    19: 'ExInitializeReadWriteLock',
    20: 'ExInterlockedAddLargeInteger',
    21: 'ExInterlockedAddLargeStatistic',
    22: 'ExInterlockedCompareExchange64',
    23: 'ExMutantObjectType',
    24: 'ExQueryPoolBlockSize',
    25: 'ExQueryNonVolatileSetting',
    26: 'ExReadWriteRefurbInfo',
    27: 'ExRaiseException',
    28: 'ExRaiseStatus',
    29: 'ExReleaseReadWriteLock',
    30: 'ExSaveNonVolatileSetting',
    31: 'ExSemaphoreObjectType',
    32: 'ExTimerObjectType',
    33: 'ExfInterlockedInsertHeadList',
    34: 'ExfInterlockedInsertTailList',
    35: 'ExfInterlockedRemoveHeadList',
    36: 'FscGetCacheSize',
    37: 'FscInvalidateIdleBlocks',
    38: 'FscSetCacheSize',
    39: 'HalClearSoftwareInterrupt',
    40: 'HalDisableSystemInterrupt',
    41: 'HalDiskCachePartitionCount',
    42: 'HalDiskModelNumber',
    43: 'HalDiskSerialNumber',
    44: 'HalEnableSystemInterrupt',
    45: 'HalGetInterruptVector',
    46: 'HalReadSMBusValue',
    47: 'HalReadWritePCISpace',
    48: 'HalRegisterShutdownNotification',
    49: 'HalRequestSoftwareInterrupt',
    50: 'HalReturnToFirmware',
    51: 'HalWriteSMBusValue',
    52: 'InterlockedCompareExchange',
    53: 'InterlockedDecrement',
    54: 'InterlockedIncrement',
    55: 'InterlockedExchange',
    56: 'InterlockedExchangeAdd',
    57: 'InterlockedFlushSList',
    58: 'InterlockedPopEntrySList',
    59: 'InterlockedPushEntrySList',
    60: 'IoAllocateIrp',
    61: 'IoBuildAsynchronousFsdRequest',
    62: 'IoBuildDeviceIoControlRequest',
    63: 'IoBuildSynchronousFsdRequest',
    64: 'IoCallDriver',
    65: 'IoCancelIrp',
    66: 'IoCheckShareAccess',
    67: 'IoCompletionObjectType',
    68: 'IoCreateDevice',
    69: 'IoCreateFile',
    70: 'IoCreateSymbolicLink',
    71: 'IoDestroyDevice',
    72: 'IoDeleteSymbolicLink',
    73: 'IoDeviceObjectType',
    74: 'IoFileObjectType',
    75: 'IoFreeIrp',
    76: 'IoInitializeIrp',
    77: 'IoInvalidDeviceRequest',
    78: 'IoQueryFileInformation',
    79: 'IoQueryVolumeInformation',
    80: 'IoQueueThreadIrp',
    81: 'IoRemoveShareAccess',
    82: 'IoSetIoCompletion',
    83: 'IoSetShareAccess',
    84: 'IoStartNextPacket',
    85: 'IoStartNextPacketByKey',
    86: 'IoStartPacket',
    87: 'IoSynchronousDeviceIoControlRequest',
    88: 'IoSynchronousFsdRequest',
    89: 'IofCallDriver',
    90: 'IofCompleteRequest',
    91: 'KdDebuggerEnabled',
    92: 'KdDebuggerNotPresent',
    93: 'IoDismountVolume',
    94: 'IoDismountVolumeByName',
    95: 'KeAlertResumeThread',
    96: 'KeAlertThread',
    97: 'KeBoostPriorityThread',
    98: 'KeBugCheck',
    99: 'KeBugCheckEx',
    100:'KeConnectInterrupt',
    101:'KeDelayExecutionThread',
    102:'KeDisconnectInterrupt',
    103:'KeEnterCriticalRegion',
    104:'MmGlobalData',
    105:'KeGetCurrentIrql',
    106:'KeGetCurrentThread',
    107:'KeInitializeApc',
    108:'KeInitializeDeviceQueue',
    109:'KeInitializeDpc',
    110:'KeInitializeEvent',
    111:'KeInitializeInterrupt',
    112:'KeInitializeMutant',
    113:'KeInitializeQueue',
    114:'KeInitializeSemaphore',
    115:'KeInitializeTimerEx',
    116:'KeInsertByKeyDeviceQueue',
    117:'KeInsertDeviceQueue',
    118:'KeInsertHeadQueue',
    119:'KeInsertQueue',
    120:'KeInsertQueueApc',
    121:'KeInsertQueueDpc',
    122:'KeIsExecutingDpc',
    123:'KeLeaveCriticalRegion',
    124:'KePulseEvent',
    125:'KeQueryBasePriorityThread',
    126:'KeQueryInterruptTime',
    127:'KeQueryPerformanceCounter',
    128:'KeQueryPerformanceFrequency',
    129:'KeQuerySystemTime',
    130:'KeRaiseIrqlToDpcLevel',
    131:'KeRaiseIrqlToSynchLevel',
    132:'KeRemoveByKeyDeviceQueue',
    133:'KeRemoveDeviceQueue',
    134:'KeRemoveEntryDeviceQueue',
    135:'KeRemoveQueue',
    136:'KeRemoveQueueDpc',
    137:'KeResetEvent',
    138:'KeRestoreFloatingPointState',
    139:'KeResumeThread',
    140:'KeRundownQueue',
    141:'KeSaveFloatingPointState',
    142:'KeSetBasePriorityThread',
    143:'KeSetDisableBoostThread',
    144:'KeSetEvent',
    145:'KeSetEventBoostPriority',
    146:'KeSetPriorityProcess',
    147:'KeSetPriorityThread',
    148:'KeSetTimer',
    149:'KeSetTimerEx',
    150:'KeStallExecutionProcessor',
    151:'KeSuspendThread',
    152:'KeSynchronizeExecution',
    153:'KeSystemTime',
    154:'KeTestAlertThread',
    155:'KeTickCount',
    156:'KeTimeIncrement',
    157:'KeWaitForMultipleObjects',
    158:'KeWaitForSingleObject',
    159:'KfAcquireSpinLock',
    160:'KfReleaseSpinLock',
    161:'KiApcList',
    162:'KiRetireDpcList',
    163:'LaunchDataPage',
    164:'MmAllocateContiguousMemory',
    165:'MmAllocateContiguousMemoryEx',
    166:'MmAllocateSystemMemory',
    167:'MmClaimGpuInstanceMemory',
    168:'MmCreateKernelStack',
    169:'MmDeleteKernelStack',
    170:'MmFreeContiguousMemory',
    171:'MmFreeSystemMemory',
    172:'MmGetPhysicalAddress',
    173:'MmIsAddressValid',
    174:'MmLockUnlockBufferPages',
    175:'MmLockUnlockPhysicalPage',
    176:'MmMapIoSpace',
    177:'MmPersistContiguousMemory',
    178:'MmQueryAddressProtect',
    179:'MmQueryAllocationSize',
    180:'MmQueryStatistics',
    181:'MmSetAddressProtect',
    182:'MmUnmapIoSpace',
    183:'NtAllocateVirtualMemory',
    184:'NtCancelTimer',
    185:'NtClearEvent',
    186:'NtClose',
    187:'NtCreateDirectoryObject',
    188:'NtCreateEvent',
    189:'NtCreateFile',
    190:'NtCreateIoCompletion',
    191:'NtCreateMutant',
    192:'NtCreateSemaphore',
    193:'NtCreateTimer',
    194:'NtDeleteFile',
    195:'NtDeviceIoControlFile',
    196:'NtDuplicateObject',
    197:'NtFlushBuffersFile',
    198:'NtFreeVirtualMemory',
    199:'NtFsControlFile',
    200:'NtOpenDirectoryObject',
    201:'NtOpenFile',
    202:'NtOpenSymbolicLinkObject',
    203:'NtProtectVirtualMemory',
    204:'NtPulseEvent',
    205:'NtQueueApcThread',
    206:'NtQueryDirectoryFile',
    207:'NtQueryDirectoryObject',
    208:'NtQueryEvent',
    209:'NtQueryFullAttributesFile',
    210:'NtQueryInformationFile',
    211:'NtQueryIoCompletion',
    212:'NtQueryMutant',
    213:'NtQuerySemaphore',
    214:'NtQuerySymbolicLinkObject',
    215:'NtQueryTimer',
    216:'NtQueryVirtualMemory',
    217:'NtQueryVolumeInformationFile',
    218:'NtReadFile',
    219:'NtReadFileScatter',
    220:'NtReleaseMutant',
    221:'NtReleaseSemaphore',
    222:'NtRemoveIoCompletion',
    223:'NtResumeThread',
    224:'NtSetEvent',
    225:'NtSetInformationFile',
    226:'NtSetIoCompletion',
    227:'NtSetSystemTime',
    228:'NtSetTimerEx',
    229:'NtSignalAndWaitForSingleObjectEx',
    230:'NtSuspendThread',
    231:'NtUserIoApcDispatcher',
    232:'NtWaitForSingleObjectEx',
    233:'NtWaitForMultipleObjectsEx',
    234:'NtWriteFile',
    235:'NtWriteFileGather',
    236:'NtYieldExecution',
    237:'ObCreateObject',
    238:'ObDirectoryObjectType',
    239:'ObInsertObject',
    240:'ObMakeTemporaryObject',
    241:'ObOpenObjectByName',
    242:'ObOpenObjectByPointer',
    243:'ObpObjectHandleTable',
    244:'ObReferenceObjectByHandle',
    245:'ObReferenceObjectByName',
    246:'ObReferenceObjectByPointer',
    247:'ObSymbolicLinkObjectType',
    248:'ObfDereferenceObject',
    249:'ObfReferenceObject',
    250:'PhyGetLinkState',
    251:'PhyInitialize',
    252:'PsCreateSystemThread',
    253:'PsCreateSystemThreadEx',
    254:'PsQueryStatistics',
    255:'PsSetCreateThreadNotifyRoutine',
    256:'PsTerminateSystemThread',
    257:'PsThreadObjectType',
    258:'RtlAnsiStringToUnicodeString',
    259:'RtlAppendStringToString',
    260:'RtlAppendUnicodeStringToString',
    261:'RtlAppendUnicodeToString',
    262:'RtlAssert',
    263:'RtlCaptureContext',
    264:'RtlCaptureStackBackTrace',
    265:'RtlCharToInteger',
    266:'RtlCompareMemory',
    267:'RtlCompareMemoryUlong',
    268:'RtlCompareString',
    269:'RtlCompareUnicodeString',
    270:'RtlCopyString',
    271:'RtlCopyUnicodeString',
    272:'RtlCreateUnicodeString',
    273:'RtlDowncaseUnicodeChar',
    274:'RtlDowncaseUnicodeString',
    275:'RtlEnterCriticalSection',
    276:'RtlEnterCriticalSectionAndRegion',
    277:'RtlEqualString',
    278:'RtlEqualUnicodeString',
    279:'RtlExtendedIntegerMultiply',
    280:'RtlExtendedLargeIntegerDivide',
    281:'RtlExtendedMagicDivide',
    282:'RtlFillMemory',
    283:'RtlFillMemoryUlong',
    284:'RtlFreeAnsiString',
    285:'RtlFreeUnicodeString',
    286:'RtlGetCallersAddress',
    287:'RtlInitAnsiString',
    288:'RtlInitUnicodeString',
    289:'RtlInitializeCriticalSection',
    290:'RtlIntegerToChar',
    291:'RtlIntegerToUnicodeString',
    292:'RtlLeaveCriticalSection',
    293:'RtlLeaveCriticalSectionAndRegion',
    294:'RtlLowerChar',
    295:'RtlMapGenericMask',
    296:'RtlMoveMemory',
    297:'RtlMultiByteToUnicodeN',
    298:'RtlMultiByteToUnicodeSize',
    299:'RtlNtStatusToDosError',
    300:'RtlRaiseException',
    301:'RtlRaiseStatus',
    302:'RtlTimeFieldsToTime',
    303:'RtlTimeToTimeFields',
    304:'RtlTryEnterCriticalSection',
    305:'RtlUlongByteSwap',
    306:'RtlUnicodeStringToAnsiString',
    307:'RtlUnicodeStringToInteger',
    308:'RtlUnicodeToMultiByteN',
    309:'RtlUnicodeToMultiByteSize',
    310:'RtlUnwind',
    311:'RtlUpcaseUnicodeChar',
    312:'RtlUpcaseUnicodeString',
    313:'RtlUpcaseUnicodeToMultiByteN',
    314:'RtlUpperChar',
    315:'RtlUpperString',
    316:'RtlUshortByteSwap',
    317:'RtlWalkFrameChain',
    318:'RtlZeroMemory',
    319:'XboxEEPROMKey',
    320:'XboxHardwareInfo',
    321:'XboxHDKey',
    322:'XboxKrnlVersion',
    323:'XboxSignatureKey',
    324:'XeImageFileName',
    325:'XeLoadSection',
    326:'XeUnloadSection',
    327:'READ_PORT_BUFFER_UCHAR',
    328:'READ_PORT_BUFFER_USHORT',
    329:'READ_PORT_BUFFER_ULONG',
    330:'WRITE_PORT_BUFFER_UCHAR',
    331:'WRITE_PORT_BUFFER_USHORT',
    332:'WRITE_PORT_BUFFER_ULONG',
    333:'XcSHAInit',
    334:'XcSHAUpdate',
    335:'XcSHAFinal',
    336:'XcRC4Key',
    337:'XcRC4Crypt',
    338:'XcHMAC',
    339:'XcPKEncPublic',
    340:'XcPKDecPrivate',
    341:'XcPKGetKeyLen',
    342:'XcVerifyPKCS1Signature',
    343:'XcModExp',
    344:'XcDESKeyParity',
    345:'XcKeyTable',
    346:'XcBlockCrypt',
    347:'XcBlockCryptCBC',
    348:'XcCryptService',
    349:'XcUpdateCrypto',
    350:'RtlRip',
    351:'XboxLANKey',
    352:'XboxAlternateSignatureKeys',
    353:'XePublicKeyData',
    354:'HalBootSMCVideoMode',
    355:'IdexChannelObject',
    356:'HalIsResetOrShutdownPending',
    357:'IoMarkIrpMustComplete',
    358:'HalInitiateShutdown',
    359:'RtlSnprintf',
    360:'RtlSprintf',
    361:'RtlVsnprintf',
    362:'RtlVsprintf',
    363:'HalEnableSecureTrayEject',
    364:'HalWriteSMCScratchRegister',
    365:'UnknownAPI365',
    366:'UnknownAPI366',
    367:'UnknownAPI367',
    368:'UnknownAPI368',
    369:'UnknownAPI369',
    370:'XProfpControl',
    371:'XProfpGetData',
    372:'IrtClientInitFast',
    373:'IrtSweep',
    374:'MmDbgAllocateMemory',
    375:'MmDbgFreeMemory',
    376:'MmDbgQueryAvailablePages',
    377:'MmDbgReleaseAddress',
    378:'MmDbgWriteCheck',
}

# ── id Tech 3 / Raven / JKA string filter patterns ────────────────────────
IDTECH3_PATTERNS = [
    # Core subsystems
    r'\bR_[A-Z][A-Za-z0-9_]+',      # renderer
    r'\bCL_[A-Z][A-Za-z0-9_]+',     # client
    r'\bSV_[A-Z][A-Za-z0-9_]+',     # server
    r'\bSys_[A-Z][A-Za-z0-9_]+',    # platform
    r'\bFS_[A-Z][A-Za-z0-9_]+',     # filesystem
    r'\bCM_[A-Z][A-Za-z0-9_]+',     # collision map
    r'\bCom_[A-Z][A-Za-z0-9_]+',    # common
    r'\bG2_[A-Z][A-Za-z0-9_]+',     # Ghoul2
    r'\bCvar_[A-Z][A-Za-z0-9_]+',   # cvar
    r'\bCmd_[A-Z][A-Za-z0-9_]+',    # command
    r'\bVM_[A-Z][A-Za-z0-9_]+',     # virtual machine
    r'\bUI_[A-Z][A-Za-z0-9_]+',     # ui
    r'\bCG_[A-Z][A-Za-z0-9_]+',     # cgame
    r'\bBG_[A-Z][A-Za-z0-9_]+',     # bg (shared)
    r'\bS_[A-Z][A-Za-z0-9_]+',      # sound
    r'\bIN_[A-Z][A-Za-z0-9_]+',     # input
    r'\bHunk_[A-Z][A-Za-z0-9_]+',   # memory
    r'\bZ_[A-Z][A-Za-z0-9_]+',      # zone memory
    r'\bSE_[A-Z][A-Za-z0-9_]+',     # string editor
    # Specific names
    r'cgame', r'uigame', r'jampgame', r'jk2mpgame',
    r'Ghoul2', r'ghoul2', r'G2Filesystem',
    r'renderer', r'Renderer',
    r'id Tech', r'Quake', r'Raven',
    r'JediAcademy', r'Jedi Academy', r'jediAcademy',
    r'VicariousVisions', r'Vicarious Visions',
    # Paths and modules
    r'\.pk3', r'base/\w+', r'base\\', r'gamedata',
    r'D:\\', r'E:\\', r'T:\\',  r'Z:\\',
    r'\\Device\\',
    # Config/cvar names
    r'r_mode', r'r_fullscreen', r'r_width', r'r_height',
    r'sv_fps', r'sv_maxclients', r'sv_hostname',
    r'com_hunkmegs', r'com_zonemegs', r'com_soundmegs',
    r'fs_game', r'fs_basepath', r'fs_cdpath', r'fs_homepath',
    r'cl_maxfps', r'cl_renderer',
    # Error / log strings
    r'Error', r'error', r'WARNING', r'warning', r'FATAL',
    r'assert', r'Assert',
    r'Init', r'Shutdown', r'Frame',
    # Xbox-specific
    r'Xbox', r'xbox', r'XDK', r'XBE',
    r'Direct3D', r'D3D', r'd3d8',
    r'XInput', r'xinput',
    r'XAudio', r'DirectSound', r'dsound',
    r'Bink', r'bink', r'RAD',
    r'imagebld', r'xbdm',
]
COMBINED = re.compile('|'.join(IDTECH3_PATTERNS))

# ── Certificate field offsets ──────────────────────────────────────────────
CERT_OFF = {
    0x00: 'Size',        0x04: 'Timestamp',
    0x08: 'TitleID',     0x0C: 'TitleNameW(80B)',
    0x5C: 'AlternateTitleIDs', 0x9C: 'AllowedMedia',
    0xA0: 'GameRegion',  0xA4: 'GameRatings',
    0xA8: 'DiskNumber',  0xAC: 'Version',
    0xB0: 'LANKey(16B)', 0xC0: 'SignatureKey(16B)',
    0xD0: 'AltSignKeys', 0x1D0:'OrigCertSize',
    0x1D4:'OnlineService',0x1D8:'SecurityFlags',
}

def load_xbe(path):
    with open(path, 'rb') as f:
        raw = f.read()
    assert raw[:4] == b'XBEH', "Not an XBE file"
    base = struct.unpack_from('<I', raw, 0x104)[0]

    def r4(off): return struct.unpack_from('<I', raw, off)[0]
    def va2off(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['roff'] + (va - s['va'])
        # fallback: try linear before sections are loaded
        if va >= base:
            return va - base
        return None

    ep_enc = r4(0x128)
    kt_enc = r4(0x158)
    ep_retail = (ep_enc ^ XBE_EP_RETAIL) & 0xFFFFFFFF
    ep_debug  = (ep_enc ^ XBE_EP_DEBUG)  & 0xFFFFFFFF
    kt_retail = (kt_enc ^ XBE_KT_RETAIL) & 0xFFFFFFFF
    kt_debug  = (kt_enc ^ XBE_KT_DEBUG)  & 0xFFFFFFFF

    img_size  = r4(0x10C)
    hdr_size  = r4(0x108)
    timestamp = r4(0x114)
    cert_va   = r4(0x118)
    sec_count = r4(0x11C)
    sec_hdr_va= r4(0x120)
    init_flags= r4(0x124)
    tls_va    = r4(0x12C)
    lib_count = r4(0x160)
    lib_va    = r4(0x164)
    klib_va   = r4(0x168)
    xapi_va   = r4(0x16C)
    debug_pn_va = r4(0x14C)
    debug_fn_va = r4(0x150)
    nonkrnl_va  = r4(0x15C)

    # Section headers
    sections = []
    sh_off = sec_hdr_va - base
    for i in range(sec_count):
        o = sh_off + i * 0x38
        flags = r4(o + 0x00)
        va    = r4(o + 0x04)
        vsz   = r4(o + 0x08)
        roff  = r4(o + 0x0C)
        rsz   = r4(o + 0x10)
        nva   = r4(o + 0x14)
        noff  = nva - base
        name  = b''
        if 0 <= noff < len(raw):
            end = raw.find(b'\x00', noff, noff + 32)
            name = raw[noff : end if end != -1 else noff+16]
        sections.append({'name': name.decode('ascii','replace').strip(),
                         'va': va, 'vsz': vsz, 'roff': roff, 'rsz': rsz,
                         'flags': flags})

    def va2off(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsz']:
                return s['roff'] + (va - s['va'])
        return None

    def read_str(va, maxlen=256):
        off = va2off(va)
        if off is None:
            off = va - base
        if not (0 <= off < len(raw)):
            return ''
        end = raw.find(b'\x00', off, off + maxlen)
        return raw[off : end if end != -1 else off+maxlen].decode('latin-1', 'replace')

    return {
        'raw': raw, 'base': base, 'sections': sections,
        'va2off': va2off, 'read_str': read_str,
        'ep_retail': ep_retail, 'ep_debug': ep_debug,
        'kt_retail': kt_retail, 'kt_debug': kt_debug,
        'img_size': img_size, 'hdr_size': hdr_size,
        'timestamp': timestamp, 'cert_va': cert_va,
        'sec_count': sec_count, 'init_flags': init_flags,
        'tls_va': tls_va, 'lib_count': lib_count,
        'lib_va': lib_va, 'klib_va': klib_va, 'xapi_va': xapi_va,
        'debug_pn_va': debug_pn_va, 'debug_fn_va': debug_fn_va,
        'nonkrnl_va': nonkrnl_va,
        'kt_enc': kt_enc, 'ep_enc': ep_enc,
    }

def dump_cert(x, out):
    raw = x['raw']
    base = x['base']
    cva = x['cert_va']
    coff = cva - base
    if not (0 <= coff < len(raw) - 0x1D8):
        out.append("  [certificate VA out of range]")
        return
    def r4(o): return struct.unpack_from('<I', raw, coff + o)[0]
    def r2(o): return struct.unpack_from('<H', raw, coff + o)[0]

    tid = r4(0x08)
    out.append(f"  TitleID:     0x{tid:08X}")
    out.append(f"  Timestamp:   0x{r4(0x04):08X}")
    out.append(f"  Region:      0x{r4(0xA0):08X}")
    out.append(f"  AllowedMedia:0x{r4(0x9C):08X}")
    out.append(f"  GameRatings: 0x{r4(0xA4):08X}")
    out.append(f"  Version:     0x{r4(0xAC):08X}")
    out.append(f"  DiskNumber:  0x{r4(0xA8):08X}")
    # Title name (UTF-16LE, 40 wchars)
    tn_off = coff + 0x0C
    tn_bytes = raw[tn_off : tn_off + 80]
    tn = tn_bytes.decode('utf-16-le', 'replace').rstrip('\x00')
    out.append(f"  TitleName:   {tn!r}")
    # LAN key (16 bytes)
    lk_off = coff + 0xB0
    lk = raw[lk_off : lk_off + 16].hex()
    out.append(f"  LANKey:      {lk}")

def dump_libraries(x, out):
    raw, base = x['raw'], x['base']
    lib_va, lib_count = x['lib_va'], x['lib_count']
    loff = lib_va - base
    out.append(f"  {'Name':<12} {'Major':>5} {'Minor':>5} {'Build':>6} {'QFE':>5} {'Flags'}")
    for i in range(lib_count):
        o = loff + i * 0x10
        if o + 0x10 > len(raw): break
        name = raw[o:o+8].rstrip(b'\x00').decode('ascii','replace')
        major, minor, build, qfe = struct.unpack_from('<HHHH', raw, o+8)
        flags_raw = qfe >> 13
        qfe_val   = qfe & 0x1FFF
        out.append(f"  {name:<12} {major:>5} {minor:>5} {build:>6} {qfe_val:>5}  0x{flags_raw:X}")

def dump_kernel_thunks(x, out):
    raw = x['raw']
    kt_va  = x['kt_retail']
    kt_off = x['va2off'](kt_va)
    if kt_off is None:
        kt_va  = x['kt_debug']
        kt_off = x['va2off'](kt_va)
    if kt_off is None:
        out.append("  [kernel thunk table not found via va2off — falling back to va-base]")
        kt_va  = x['kt_retail']
        kt_off = kt_va - x['base']
    if not (0 <= kt_off < len(raw)):
        out.append("  [kernel thunk table not found]")
        return

    out.append(f"  Thunk table VA: 0x{kt_va:08X}  (file offset 0x{kt_off:X})")
    out.append(f"  {'Index':>5}  {'Ordinal':>7}  {'Name'}")
    i = 0
    while kt_off + i*4 + 4 <= len(raw):
        val = struct.unpack_from('<I', raw, kt_off + i*4)[0]
        if val == 0:
            break
        # Thunk entries have bit 31 set (0x80000000 | ordinal)
        if not (val & 0x80000000):
            out.append(f"  [unexpected entry 0x{val:08X} at index {i} — bit31 not set; stopping]")
            break
        ordinal = val & 0x7FFFFFFF
        name = KRNL.get(ordinal, f'ordinal_{ordinal}')
        out.append(f"  {i:>5}  {ordinal:>7}  {name}")
        i += 1
    out.append(f"  Total: {i} thunks")

def dump_tls(x, out):
    raw = x['raw']
    tva = x['tls_va']
    if not tva:
        out.append("  [no TLS directory]")
        return
    toff = x['va2off'](tva)
    if toff is None:
        out.append(f"  [TLS VA 0x{tva:08X} — not in any section]")
        return
    if not (0 <= toff < len(raw) - 0x18):
        out.append(f"  [TLS VA 0x{tva:08X} file offset 0x{toff:X} out of range]")
        return
    raw_va  = struct.unpack_from('<I', raw, toff+0x00)[0]
    raw_end = struct.unpack_from('<I', raw, toff+0x04)[0]
    idx_va  = struct.unpack_from('<I', raw, toff+0x08)[0]
    cb_va   = struct.unpack_from('<I', raw, toff+0x0C)[0]
    sz_zero = struct.unpack_from('<I', raw, toff+0x10)[0]
    flags   = struct.unpack_from('<I', raw, toff+0x14)[0]
    tls_size = raw_end - raw_va
    out.append(f"  TLS data:  0x{raw_va:08X} – 0x{raw_end:08X}  ({tls_size} bytes)")
    out.append(f"  Index VA:  0x{idx_va:08X}")
    out.append(f"  Callbacks: 0x{cb_va:08X}")
    out.append(f"  SizeOfZeroFill: {sz_zero}")
    out.append(f"  Flags:     0x{flags:08X}")
    # Enumerate callbacks
    if cb_va:
        coff = cb_va - base
        j = 0
        while coff + j*4 + 4 <= len(raw):
            cba = struct.unpack_from('<I', raw, coff + j*4)[0]
            if cba == 0: break
            out.append(f"  Callback[{j}]: 0x{cba:08X}")
            j += 1

def extract_strings(raw, min_len=5):
    """Extract all printable ASCII strings >= min_len from binary blob."""
    results = []
    current = []
    start = 0
    for i, b in enumerate(raw):
        if 0x20 <= b < 0x7F:
            if not current:
                start = i
            current.append(chr(b))
        else:
            if len(current) >= min_len:
                s = ''.join(current)
                results.append((start, s))
            current = []
    if len(current) >= min_len:
        results.append((start, ''.join(current)))
    return results

def va_of_offset(sections, off):
    for s in sections:
        if s['roff'] <= off < s['roff'] + s['rsz']:
            return s['va'] + (off - s['roff'])
    return None

def section_of_va(sections, va):
    for s in sections:
        if s['va'] <= va < s['va'] + s['vsz']:
            return s['name']
    return '???'

def main():
    xbe_path = sys.argv[1] if len(sys.argv) > 1 else \
        r'Star Wars Jedi Academy game\default.xbe'

    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir    = os.path.join(script_dir, 'output')
    os.makedirs(out_dir, exist_ok=True)
    inv_path = os.path.join(out_dir, 'phase1_inventory.txt')
    str_path = os.path.join(out_dir, 'phase1_strings.txt')

    x = load_xbe(xbe_path)
    raw, base, sections = x['raw'], x['base'], x['sections']

    lines = []
    def H(title): lines.append(''); lines.append('=' * 72); lines.append(title); lines.append('=' * 72)

    # ── Header summary ──────────────────────────────────────────────────────
    H("XBE HEADER")
    import datetime
    try:
        ts = datetime.datetime.utcfromtimestamp(x['timestamp'])
        ts_str = ts.strftime('%Y-%m-%d %H:%M:%S UTC')
    except:
        ts_str = f"0x{x['timestamp']:08X}"
    lines.append(f"  File:             {os.path.abspath(xbe_path)}")
    lines.append(f"  File size:        {len(raw):,} bytes  (0x{len(raw):X})")
    lines.append(f"  Base address:     0x{base:08X}")
    lines.append(f"  Image size:       0x{x['img_size']:08X}  ({x['img_size']:,} bytes)")
    lines.append(f"  Header size:      0x{x['hdr_size']:08X}")
    lines.append(f"  Timestamp:        {ts_str}")
    lines.append(f"  Init flags:       0x{x['init_flags']:08X}")
    lines.append(f"  Entry point:      0x{x['ep_retail']:08X}  [retail decrypt]")
    lines.append(f"  Entry point dbg:  0x{x['ep_debug']:08X}  [debug decrypt]")
    lines.append(f"  Kernel thunk:     0x{x['kt_retail']:08X}  [retail decrypt]")
    lines.append(f"  Kernel thunk dbg: 0x{x['kt_debug']:08X}  [debug decrypt]")
    lines.append(f"  Debug pathname:   {x['read_str'](x['debug_pn_va'])!r}")
    lines.append(f"  Debug filename:   {x['read_str'](x['debug_fn_va'])!r}")
    lines.append(f"  Sections:         {x['sec_count']}")
    lines.append(f"  Library versions: {x['lib_count']}")

    # ── Certificate ─────────────────────────────────────────────────────────
    H("CERTIFICATE")
    dump_cert(x, lines)

    # ── Section table ────────────────────────────────────────────────────────
    H("SECTION TABLE")
    lines.append(f"  {'#':>2}  {'Name':<16}  {'VA':>10}  {'VSize':>8}  {'FileOff':>8}  {'FileSize':>8}  Flags")
    for i, s in enumerate(sections):
        f_wr  = 'W' if s['flags'] & 0x01 else '-'
        f_pre = 'P' if s['flags'] & 0x04 else '-'
        f_exe = 'X' if s['flags'] & 0x08 else '-'
        f_ro  = 'R' if not (s['flags'] & 0x01) else '-'
        lines.append(f"  {i:>2}  {s['name']:<16}  0x{s['va']:08X}  "
                     f"0x{s['vsz']:06X}  0x{s['roff']:06X}  0x{s['rsz']:06X}  "
                     f"0x{s['flags']:08X} {f_ro}{f_wr}{f_exe}{f_pre}")
    # Totals
    total_vsz = sum(s['vsz'] for s in sections)
    total_rsz = sum(s['rsz'] for s in sections)
    lines.append(f"  {'':>2}  {'TOTAL':<16}  {'':>10}  0x{total_vsz:06X}  {'':>8}  0x{total_rsz:06X}")

    # ── Library versions ────────────────────────────────────────────────────
    H("LIBRARY VERSIONS")
    dump_libraries(x, lines)

    # ── TLS directory ────────────────────────────────────────────────────────
    H("TLS DIRECTORY")
    dump_tls(x, lines)

    # ── Kernel thunk table ───────────────────────────────────────────────────
    H("KERNEL THUNK TABLE")
    dump_kernel_thunks(x, lines)

    # ── Entry point context ──────────────────────────────────────────────────
    H("ENTRY POINT")
    ep = x['ep_retail']
    ep_sec = section_of_va(sections, ep)
    lines.append(f"  VA: 0x{ep:08X}  section: {ep_sec}")
    ep_off = x['va2off'](ep)
    if ep_off is not None and ep_off + 64 <= len(raw):
        lines.append(f"  First 64 bytes:")
        chunk = raw[ep_off : ep_off + 64]
        for j in range(0, len(chunk), 16):
            row = chunk[j:j+16]
            hex_part  = ' '.join(f'{b:02X}' for b in row)
            ascii_part = ''.join(chr(b) if 0x20 <= b < 0x7F else '.' for b in row)
            lines.append(f"    0x{ep+j:08X}:  {hex_part:<48}  {ascii_part}")

    # ── Section content summary ───────────────────────────────────────────────
    H("SECTION CONTENT SUMMARY (code/data bytes, entropy hint)")
    for s in sections:
        if s['rsz'] == 0:
            lines.append(f"  {s['name']:<16}  [no file data — BSS/zero-init]")
            continue
        chunk = raw[s['roff'] : s['roff'] + min(s['rsz'], 65536)]
        unique = len(set(chunk))
        null_pct = chunk.count(0) * 100 // len(chunk)
        text_bytes = sum(1 for b in chunk if 0x20 <= b < 0x7F)
        text_pct   = text_bytes * 100 // len(chunk)
        lines.append(f"  {s['name']:<16}  unique_bytes={unique}/256  null={null_pct}%  printable={text_pct}%  "
                     f"file_sz=0x{s['rsz']:X}")

    # Write inventory
    with open(inv_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"Inventory written: {inv_path}")

    # ── String extraction and filter ──────────────────────────────────────────
    all_strings = extract_strings(raw, min_len=5)
    filtered = []
    for off, s in all_strings:
        if COMBINED.search(s):
            va = va_of_offset(sections, off)
            sec = section_of_va(sections, va) if va else '???'
            va_str = f"0x{va:08X}" if va else f"file+0x{off:X}"
            filtered.append((va_str, sec, s))

    str_lines = [
        "Phase 1 — Filtered strings (id Tech 3 / Raven / Xbox identifiers)",
        f"Total strings scanned: {len(all_strings)}  Matching: {len(filtered)}",
        "",
        f"  {'VA':>10}  {'Section':<16}  String",
        "-" * 80,
    ]
    for va_str, sec, s in filtered:
        str_lines.append(f"  {va_str}  {sec:<16}  {s[:120]}")

    with open(str_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(str_lines) + '\n')
    print(f"Strings written:   {str_path}")

    # Also echo key header lines to stdout for quick review
    print()
    for l in lines:
        print(l)

if __name__ == '__main__':
    main()
