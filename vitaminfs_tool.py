#!/usr/bin/env python3
import sys
import struct
import os
import subprocess

SECTOR_SIZE = 512
SUPERBLOCK_LBA = 1
INODE_START_LBA = 2
INODE_COUNT = 60
DATA_START_LBA = 22
MAGIC = 0x41544956

TYPE_FREE = 0
TYPE_FILE = 1
TYPE_DIR = 2

INODE_FMT = "<64sIIII48s"
SB_FMT = "<IIII"

def read_sector(f, lba):
    f.seek(lba * SECTOR_SIZE)
    return bytearray(f.read(SECTOR_SIZE))

def write_sector(f, lba, data):
    if len(data) < SECTOR_SIZE:
        data = data + b'\x00' * (SECTOR_SIZE - len(data))
    f.seek(lba * SECTOR_SIZE)
    f.write(data[:SECTOR_SIZE])

def get_inode(f, idx):
    inodes_per_sector = SECTOR_SIZE // 128
    lba = INODE_START_LBA + (idx // inodes_per_sector)
    offset = (idx % inodes_per_sector) * 128
    sector = read_sector(f, lba)
    inode_bytes = sector[offset:offset+128]

    name, itype, parent, size, first_block, res = struct.unpack(INODE_FMT, inode_bytes)
    name = name.decode('utf-8', errors='ignore').strip('\x00')
    return {"idx": idx, "name": name, "type": itype, "parent": parent, "size": size, "first_block": first_block}

def write_inode(f, idx, inode_dict):
    inodes_per_sector = SECTOR_SIZE // 128
    lba = INODE_START_LBA + (idx // inodes_per_sector)
    offset = (idx % inodes_per_sector) * 128
    sector = read_sector(f, lba)

    name_bytes = inode_dict["name"].encode('utf-8')[:64].ljust(64, b'\x00')
    res_bytes = b'\x00' * 48
    inode_bytes = struct.pack(INODE_FMT, name_bytes, inode_dict["type"], inode_dict["parent"], inode_dict["size"], inode_dict["first_block"], res_bytes)
    sector[offset:offset+128] = inode_bytes
    write_sector(f, lba, sector)

def cmd_init(img_path):
    print(f"[+] Создание пустого диска {img_path} (Увеличен до 2000 секторов)...")
    with open(img_path, "wb") as f:
        f.write(b'\x00' * SECTOR_SIZE * 2000)

    with open(img_path, "r+b") as f:
        sb_bytes = struct.pack(SB_FMT, MAGIC, INODE_COUNT, DATA_START_LBA, 0)
        write_sector(f, SUPERBLOCK_LBA, sb_bytes)

        root_inode = {"name": "/", "type": TYPE_DIR, "parent": 0, "size": 0, "first_block": 0}
        write_inode(f, 0, root_inode)
    print("[+] VitaminFS успешно инициализирована!")

def cmd_ls(img_path):
    if not os.path.exists(img_path): return print("[-] Диск не найден.")
    with open(img_path, "rb") as f:
        print(f"Список файлов VitaminFS:")
        print(f"{'Имя':<20} | {'Тип':<10} | {'Размер (байт)':<12} | {'Блок данных'}")
        print("-" * 60)
        for i in range(INODE_COUNT):
            inode = get_inode(f, i)
            if inode["type"] != TYPE_FREE:
                t_str = "DIR" if inode["type"] == TYPE_DIR else "FILE"
                print(f"{inode['name']:<20} | {t_str:<10} | {inode['size']:<12} | {inode['first_block']}")

def cmd_add(img_path, host_path, vfs_name):
    if not os.path.exists(img_path): return print("[-] Диск не найден.")
    if not os.path.exists(host_path): return print(f"[-] Файл на хосте {host_path} не найден.")

    with open(host_path, "rb") as hf:
        file_data = hf.read()

    if len(file_data) > 32768:
        return print("[-] Ошибка: Размер файла превышает хардлимит ядра в 32 КБ!")

    blocks_needed = (len(file_data) + 511) // 512
    if blocks_needed == 0: blocks_needed = 1

    with open(img_path, "r+b") as f:
        sb_sector = read_sector(f, SUPERBLOCK_LBA)
        magic, total_inodes, data_start, next_free_block = struct.unpack(SB_FMT, sb_sector[:16])

        target_inode_idx = -1
        for i in range(INODE_COUNT):
            inode = get_inode(f, i)
            if inode["type"] == TYPE_FREE:
                target_inode_idx = i
                break

        if target_inode_idx == -1: return print("[-] Нет свободных инодов!")

        for i in range(blocks_needed):
            chunk = file_data[i*512 : (i+1)*512]
            write_sector(f, data_start + next_free_block + i, chunk)

        new_inode = {
            "name": vfs_name,
            "type": TYPE_FILE,
            "parent": 0,
            "size": len(file_data),
            "first_block": next_free_block
        }
        write_inode(f, target_inode_idx, new_inode)

        sb_bytes = struct.pack(SB_FMT, magic, total_inodes, data_start, next_free_block + blocks_needed)
        write_sector(f, SUPERBLOCK_LBA, sb_bytes)

        print(f"[+] Файл '{host_path}' импортирован как '{vfs_name}'. Занято секторов: {blocks_needed}")

def cmd_build_app(img_path, c_file_path, vfs_name):
    obj_file = "app.o"
    bin_file = "app.bin"
    print(f"[+] Компиляция {c_file_path} под архитектуру VitaminOS...")

    compile_cmd = [
        "gcc", "-m32", "-march=i386", "-ffreestanding", "-fno-pie", "-nostdlib",
        "-O0", "-fno-stack-protector", "-c", c_file_path, "-o", obj_file
    ]

    link_cmd = [
        "ld", "-m", "elf_i386", "-Ttext", "0x50000", "--oformat", "binary",
        obj_file, "-o", bin_file
    ]

    try:
        subprocess.run(compile_cmd, check=True)
        subprocess.run(link_cmd, check=True)
        cmd_add(img_path, bin_file, vfs_name)
    except subprocess.CalledProcessError as e:
        print(f"[-] Ошибка компиляции! Код: {e}")
    finally:
        if os.path.exists(obj_file): os.remove(obj_file)
        if os.path.exists(bin_file): os.remove(bin_file)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Использование:\n  init <образ>\n  ls <образ>\n  add <образ> <файл> <имя>\n  build <образ> <си_файл> <имя>")
        sys.exit(1)

    cmd = sys.argv[1]
    img = sys.argv[2]

    if cmd == "init": cmd_init(img)
    elif cmd == "ls": cmd_ls(img)
    elif cmd == "add" and len(sys.argv) == 5: cmd_add(img, sys.argv[3], sys.argv[4])
    elif cmd == "build" and len(sys.argv) == 5: cmd_build_app(img, sys.argv[3], sys.argv[4])
