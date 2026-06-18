MBOOT_PAGE_ALIGN    equ 1 << 0
MBOOT_MEMORY_INFO   equ 1 << 1
MBOOT_HEADER_MAGIC  equ 0x1BADB002
MBOOT_HEADER_FLAGS  equ MBOOT_PAGE_ALIGN | MBOOT_MEMORY_INFO
MBOOT_CHECKSUM      equ -(MBOOT_HEADER_MAGIC + MBOOT_HEADER_FLAGS)

section .multiboot
align 4
	dd MBOOT_HEADER_MAGIC
	dd MBOOT_HEADER_FLAGS
	dd MBOOT_CHECKSUM

section .bss
align 16
stack_bottom:
	resb 16384
stack_top:


section .data
align 4
gdt_start:
	dq 0x0000000000000000
	dq 0x00CF9A000000FFFF
	dq 0x00CF92000000FFFF
gdt_end:

gdt_ptr:
	dw gdt_end - gdt_start - 1
	dd gdt_start

section .text
global _start
_start:
	cli


	lgdt [gdt_ptr]


	jmp 0x08:.reload_cs

.reload_cs:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax


	mov esp, stack_top
	extern kernel_main
	call kernel_main

.hang:
	cli
	hlt
	jmp .hang


global load_idt
load_idt:
	mov eax, [esp + 4]
	lidt [eax]
	ret

global isr_dummy
isr_dummy:
	pushad
	mov al, 0x20
	out 0x20, al
	out 0xA0, al
	popad
	iretd

global isr_kbd
extern keyboard_handler
isr_kbd:
	pushad
	cld
	call keyboard_handler
	popad
	iretd

global isr40
extern syscall_handler
isr40:
	pushad
	push esp
	call syscall_handler
	add esp, 4
	popad
	iretd
