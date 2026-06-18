                                                  Vitamin OS
<img width="717" height="396" alt="изображение" src="https://github.com/user-attachments/assets/e8883d6b-3e4a-4235-ae32-2b78bd28692d" />

A 32-bit operating system based on the i386 architecture with a monolithic kernel, its own file system (VitaminFS), and a command shell (v-shell).
Dependencies
To compile the source code, build the ISO image, and run it in the emulator, install the necessary packages:
sudo apt update
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin xorriso python3 gcc-multilib

Project Structure
* boot/ — Bootloader (Multiboot)
* kernel/ — Kernel, driver, and shell source code
* programs/ — User applications (User Space)
* vitaminfs_tool.py — Python utility for working with disk images
* Makefile — Automated build script
Build and Run
To automatically compile the kernel, generate the disk, embed programs, and run the OS in the QEMU emulator, run the following in the project root:
make run-iso

Data is saved to the virtual hard disk disk.img in ATA PIO mode and remains available after rebooting the emulator. To clean up temporary build files:
make clean

File System (VitaminFS)
Custom file system with contiguous block allocation.
* Sector size: 512 bytes.
* Inode size: 128 bytes.
* Maximum file size: 32 KB.
To manage the disk from the host system, use the vitaminfs_tool.py script:
View disk contents
python3 vitaminfs_tool.py ls disk.img

Build a C file into a binary and write it to disk
python3 vitaminfs_tool.py build disk.img programs/myapp.c myapp.bin

Development for Vitamin OS
The standard C library (libc) is not supported in the OS. Applications are compiled into raw binaries.
System Calls (Interrupt 0x40)
Interaction with the kernel occurs through processor registers:
* EAX = 1: Output a string (EBX = pointer to a string)
* EAX = 2: Read input (EBX = pointer to a buffer, ECX = maximum length)
* EAX = 3: Output a number (EBX = integer)
Entry Point
The main architectural requirement: the entry point function (main) must always be the first function in your .c file. The kernel loads the program into memory and transfers control to the very first byte of the file. All auxiliary functions must be implemented below main.
To permanently add your program to the system, save it to the programs/ folder and include the build command using vitaminfs_tool.py in the disk.img block of the Makefile.





