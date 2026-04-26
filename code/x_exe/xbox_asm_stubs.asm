; xbox_asm_stubs.asm
.386
.model flat
OPTION CASEMAP:NONE

EXTERN _mainCRTStartup:NEAR
EXTERN _XBLog_PreCRTProbe:NEAR
EXTERN _XBLog_PostCRTProbe:NEAR

.data
default_fpu_cw dw 027Fh

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

PUBLIC __except_handler4
__except_handler4 PROC NEAR
    xor     eax, eax
    ret
__except_handler4 ENDP

; Entry points - initialize FPU, then hand off to the CRT startup.
; This preserves our early Xbox-specific setup while still letting the
; CRT initialize stdio and other runtime state before main() runs.
PUBLIC _WinMainCRTStartup
_WinMainCRTStartup PROC NEAR
    finit                   ; Initialize FPU - fixes R6002
    fldcw   default_fpu_cw  ; Load the standard x87 control word
    call    _XBLog_PreCRTProbe  ; write "precrt_ok" before _mainCRTStartup runs
    call    _mainCRTStartup     ; spawns game thread + returns (or calls XapiBootToDash)
    call    _XBLog_PostCRTProbe ; write "post_crt" if _mainCRTStartup returned normally
@@spin:
    jmp     @@spin
_WinMainCRTStartup ENDP

END
