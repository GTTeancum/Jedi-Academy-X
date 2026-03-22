; xbox_asm_stubs.asm
; Confirmed symbol names via dumpbin:
;   callers need  __ftol2_sse        (2 underscores)
;   callers need  ___CxxFrameHandler3 (3 underscores)
;   linker needs  _WinMainCRTStartup  (1 underscore)
; .model flat does NOT prepend underscores - write exact names.

.386
.model flat
OPTION CASEMAP:NONE

.code

PUBLIC __ftol2_sse
__ftol2_sse PROC NEAR
    sub     esp, 4
    fstcw   word ptr [esp]
    mov     ax, word ptr [esp]
    or      ax, 0C00h
    sub     esp, 2
    mov     word ptr [esp], ax
    fldcw   word ptr [esp]
    add     esp, 2
    fld     qword ptr [esp+4+4]
    fistp   dword ptr [esp+4+4]
    fldcw   word ptr [esp]
    add     esp, 4
    mov     eax, dword ptr [esp+4]
    ret
__ftol2_sse ENDP

PUBLIC __ftol2
__ftol2 PROC NEAR
    sub     esp, 4
    fstcw   word ptr [esp]
    mov     ax, word ptr [esp]
    or      ax, 0C00h
    sub     esp, 2
    mov     word ptr [esp], ax
    fldcw   word ptr [esp]
    add     esp, 2
    fld     qword ptr [esp+4+4]
    fistp   dword ptr [esp+4+4]
    fldcw   word ptr [esp]
    add     esp, 4
    mov     eax, dword ptr [esp+4]
    ret
__ftol2 ENDP

PUBLIC ___CxxFrameHandler3
___CxxFrameHandler3 PROC NEAR
    mov     eax, 1
    ret
___CxxFrameHandler3 ENDP

; _WinMainCRTStartup - exact 1-underscore name the linker expects
; Just spin forever; real Xbox startup is in xapilibd.lib
PUBLIC _WinMainCRTStartup
_WinMainCRTStartup PROC NEAR
@@spin:
    jmp     @@spin
_WinMainCRTStartup ENDP

END
