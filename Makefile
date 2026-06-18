ASM = nasm
CC = gcc
LD = ld

CFLAGS = -ffreestanding -O0 -Wall -Wextra -nostdlib -c -std=gnu99 -m32 -fno-pie -fno-pic -fno-stack-protector -mpreferred-stack-boundary=2
LDFLAGS_KERNEL = -T linker.ld -ffreestanding -nostdlib -m32 -no-pie

all: iso

boot/multiboot.o: boot/multiboot.asm
	mkdir -p boot
	$(ASM) -f elf32 $< -o $@

kernel/kernel.o: kernel/kernel.c
	mkdir -p kernel
	$(CC) $(CFLAGS) $< -o $@

vitamin.bin: boot/multiboot.o kernel/kernel.o
	$(CC) $(LDFLAGS_KERNEL) -o $@ $^


disk.img: programs/calc.c
	@echo "[!] Инициализация диска VitaminFS..."
	python3 vitaminfs_tool.py init disk.img
	@echo "[!] Сборка и внедрение программ..."
	python3 vitaminfs_tool.py build disk.img programs/calc.c calc.bin


iso: vitamin.bin disk.img grub.cfg
	mkdir -p iso/boot/grub
	cp vitamin.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	cp disk.img iso/
	grub-mkrescue -o vitamin.iso iso/

run-iso: iso
	qemu-system-i386 -cdrom vitamin.iso -drive file=disk.img,format=raw,if=ide,index=0,media=disk -boot d -m 128M

clean:
	rm -f boot/*.o kernel/*.o programs/*.o programs/*.bin *.bin *.img *.iso
	rm -rf iso

.PHONY: all iso run-iso clean
