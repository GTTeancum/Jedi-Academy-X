; xbox_asm_stubs.asm
; Confirmed symbol names via dumpbin:
;   callers need  __ftol2_sse        (2 underscores)
;   callers need  ___CxxFrameHandler3 (3 underscores)
;   linker needs  _WinMainCRTStartup  (1 underscore)
; .model flat does NOT prepend underscores - write exact names.

.386
.model flat
OPTION CASEMAP:NONE

.data
default_fpu_cw dw 027Fh

.code

EXTERN _mainCRTStartup:PROC
EXTERN _XBLog_PreCRTProbe:PROC
EXTERN _XBLog_PostCRTProbe:PROC

PUBLIC __ftol2_sse
__ftol2_sse PROC NEAR
    sub     esp, 12
    fstcw   word ptr [esp+8]
    mov     ax, word ptr [esp+8]
    or      ax, 0C00h
    mov     word ptr [esp+10], ax
    fldcw   word ptr [esp+10]
    fistp   qword ptr [esp]
    fldcw   word ptr [esp+8]
    mov     eax, dword ptr [esp]
    mov     edx, dword ptr [esp+4]
    add     esp, 12
    ret
__ftol2_sse ENDP

PUBLIC __ftol2
__ftol2 PROC NEAR
    sub     esp, 12
    fstcw   word ptr [esp+8]
    mov     ax, word ptr [esp+8]
    or      ax, 0C00h
    mov     word ptr [esp+10], ax
    fldcw   word ptr [esp+10]
    fistp   qword ptr [esp]
    fldcw   word ptr [esp+8]
    mov     eax, dword ptr [esp]
    mov     edx, dword ptr [esp+4]
    add     esp, 12
    ret
__ftol2 ENDP

PUBLIC ___CxxFrameHandler3
___CxxFrameHandler3 PROC NEAR
    mov     eax, 1
    ret
___CxxFrameHandler3 ENDP

; _WinMainCRTStartup - exact 1-underscore name the linker expects.
; Initialize the FPU, then hand off to the CRT startup so XAPI/CRT state is
; ready before the MP Xbox main() runs.
PUBLIC _WinMainCRTStartup
_WinMainCRTStartup PROC NEAR
    finit
    fldcw   default_fpu_cw
    call    _XBLog_PreCRTProbe
    call    _mainCRTStartup
    call    _XBLog_PostCRTProbe
@@spin:
    jmp     @@spin
_WinMainCRTStartup ENDP

PUBLIC __except_handler4
__except_handler4 PROC NEAR
    xor eax, eax
    ret
__except_handler4 ENDP

END
