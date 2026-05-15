; Notepatra palette preview — synthetic; no real data
; x86-64 Linux NASM demo: writes "Hello, Alice!\n" and exits.

bits 64

section .data
    msg     db  "Hello, Alice!", 0Ah
    msglen  equ $ - msg
    pi_str  db  "pi=3.14", 0Ah
    pi_len  equ $ - pi_str

section .bss
    buffer  resb 64
    counter resq 1

section .text
    global _start

_start:
    ; write(stdout, msg, msglen)
    mov     rax, 1              ; syscall: write
    mov     rdi, 1              ; fd: stdout
    lea     rsi, [rel msg]
    mov     rdx, msglen
    syscall

    ; write(stdout, pi_str, pi_len)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel pi_str]
    mov     rdx, pi_len
    syscall

    ; simple loop: count from 0 to 4
    xor     rcx, rcx
.loop:
    cmp     rcx, 5
    jge     .done
    inc     rcx
    jmp     .loop

.done:
    mov     [rel counter], rcx

    ; arithmetic demo
    mov     rax, 40
    add     rax, 2              ; rax = 42
    sub     rax, 0
    test    rax, rax
    jz      .exit_zero

    ; exit(0)
    mov     rax, 60             ; syscall: exit
    xor     rdi, rdi
    syscall

.exit_zero:
    mov     rax, 60
    mov     rdi, 1
    syscall
