#include <stdint.h>

#define VIDEO_MEMORY 0xB8000
#define MAX_CMD_LEN 256
#define MAX_PATH_LEN 256
#define MAX_FILE_SIZE 32768

#define COLOR_WHITE        0x07
#define COLOR_GRAY         0x08
#define COLOR_GREEN        0x02
#define COLOR_RED          0x04
#define COLOR_CYAN         0x03
#define COLOR_YELLOW       0x06
#define COLOR_MAGENTA      0x05
#define COLOR_BRIGHT_GREEN 0x0A

#define ATA_REG_DATA        0x1F0
#define ATA_REG_SECCOUNT    0x1F2
#define ATA_REG_LBA_LO      0x1F3
#define ATA_REG_LBA_MID     0x1F4
#define ATA_REG_LBA_HI      0x1F5
#define ATA_REG_DRV_HEAD    0x1F6
#define ATA_REG_STATUS      0x1F7
#define ATA_REG_COMMAND     0x1F7

#define ATA_CMD_READ  0x20
#define ATA_CMD_WRITE 0x30
#define STATUS_BSY  0x80
#define STATUS_DRQ  0x08

#define SECTOR_SIZE 512
#define SUPERBLOCK_LBA 1
#define INODE_START_LBA 2
#define INODE_COUNT 60
#define DATA_START_LBA 22

#define TYPE_FREE 0
#define TYPE_FILE 1
#define TYPE_DIR  2

typedef struct {
    char name[64];
    uint32_t type;
    uint32_t parent_inode;
    uint32_t size;
    uint32_t first_block;
    uint32_t reserved[12];
} __attribute__((packed)) vfs_inode_t;

typedef struct {
    uint32_t magic;
    uint32_t total_inodes;
    uint32_t data_start_block;
    uint32_t next_free_block;
} __attribute__((packed)) superblock_t;

typedef struct { uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; } __attribute__((packed)) registers_t;
typedef struct { uint16_t base_low; uint16_t selector; uint8_t zero; uint8_t flags; uint16_t base_high; } __attribute__((packed)) idt_entry_t;
typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt_ptr_t;

static uint16_t* video = (uint16_t*)VIDEO_MEMORY;
static int cursor_x = 0, cursor_y = 0;
char cmd_buffer[MAX_CMD_LEN];
char current_path[MAX_PATH_LEN] = "/";
uint32_t current_dir_inode = 0;
uint32_t kernel_transactions = 0;

uint8_t shared_file_buf[MAX_FILE_SIZE];
uint8_t clipboard_buf[MAX_FILE_SIZE];
char clipboard_name[64] = {0};
uint32_t clipboard_size = 0;
int clipboard_has_data = 0;

idt_entry_t idt[256];
idt_ptr_t idt_ptr;
extern void load_idt(uint32_t ptr);

volatile uint32_t rand_seed = 98765;

void cmd_help(const char* arg);
void cmd_vitamin(void);
void cmd_fastfetch(void);
void cmd_top(void);
void cmd_locate(const char* arg);
void cmd_cp(const char* arg);
void cmd_pas(void);
void cmd_cppas(char* args);
void cmd_ls(void);
void cmd_cd(const char* arg);
void cmd_mkdir(const char* arg);
void cmd_touch(const char* arg);
void cmd_rmdir(const char* arg);
void cmd_rm(const char* arg);
void cmd_cat(const char* arg);
void cmd_echo(char* arg);
void cmd_run(const char* arg);
void cmd_edit(const char* arg);
void cmd_hexdump(const char* arg);

int strlen(const char* s) { int n = 0; while (s[n]) n++; return n; }
void strcpy(char* d, const char* s) { while (*s) *d++ = *s++; *d = 0; }
int strcmp(const char* a, const char* b) { while (*a && *b && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }
char* strchr(const char* s, int c) { while (*s) { if (*s == c) return (char*)s; s++; } return 0; }
void strcat(char* d, const char* s) { while (*d) d++; strcpy(d, s); }

void split_args(char* args, char** a1, char** a2) {
    *a1 = args; *a2 = 0;
    while (**a1 == ' ') (*a1)++;
    char* p = *a1;
    while (*p && *p != ' ') p++;
    if (*p) { *p = 0; p++; while (*p == ' ') p++; if (*p) *a2 = p; }
}

static inline void io_wait(void) { asm volatile ("outb %%al, $0x80" : : "a"(0)); }
uint8_t inb(uint16_t port) { uint8_t ret; asm volatile ("inb %1, %0" : "=a"(ret) : "d"(port)); return ret; }
void outb(uint16_t port, uint8_t val) { asm volatile ("outb %0, %1" : : "a"(val), "d"(port)); }
static inline uint16_t inw(uint16_t port) { uint16_t ret; asm volatile ("inw %1, %0" : "=a"(ret) : "d"(port)); return ret; }
static inline void outw(uint16_t port, uint16_t val) { asm volatile ("outw %0, %1" : : "a"(val), "d"(port)); }

void scroll() {
    for (int i = 0; i < 24 * 80; i++) video[i] = video[i + 80];
    for (int i = 24 * 80; i < 25 * 80; i++) video[i] = ' ' | (COLOR_WHITE << 8);
    cursor_y = 24; cursor_x = 0;
}

void putchar_color(char c, uint8_t color) {
    if (c == '\n') {
        cursor_x = 0; cursor_y++; if (cursor_y >= 25) scroll();
    } else if (c == '\b') {
        if (cursor_x > 0) { cursor_x--; video[cursor_y * 80 + cursor_x] = ' ' | (color << 8); }
    } else if ((unsigned char)c >= ' ') {
        video[cursor_y * 80 + cursor_x] = c | (color << 8);
        cursor_x++; if (cursor_x >= 80) { cursor_x = 0; cursor_y++; if (cursor_y >= 25) scroll(); }
    }
}

void print_color(const char* s, uint8_t color) { while (*s) putchar_color(*s++, color); }
void print(const char* s) { print_color(s, COLOR_WHITE); }
void print_int(int n) { if (n < 0) { putchar_color('-', COLOR_WHITE); n = -n; } if (n >= 10) print_int(n / 10); putchar_color('0' + n % 10, COLOR_WHITE); }
void clear_screen() { for (int i = 0; i < 80 * 25; i++) video[i] = ' ' | (COLOR_WHITE << 8); cursor_x = cursor_y = 0; }

void update_vga_cursor(int x, int y) {
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

volatile int kbd_ready = 0;
volatile int shift_active = 0;
volatile int kbd_extended = 0;
volatile int last_keycode = 0;

#define HISTORY_MAX 10
char cmd_history[HISTORY_MAX][MAX_CMD_LEN];
int history_count = 0;

#define KEY_UP    1001
#define KEY_DOWN  1002
#define KEY_LEFT  1003
#define KEY_RIGHT 1004

static const char kbd_map_normal[] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' '
};

static const char kbd_map_shift[] = {
    0,0,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' '
};

void keyboard_handler() {
    uint8_t sc = inb(0x60);
    rand_seed += sc;

    if (sc == 0xE0) { kbd_extended = 1; outb(0x20, 0x20); return; }

    if (kbd_extended) {
        kbd_extended = 0;
        if (sc == 0x48) last_keycode = KEY_UP;
        else if (sc == 0x50) last_keycode = KEY_DOWN;
        else if (sc == 0x4B) last_keycode = KEY_LEFT;
        else if (sc == 0x4D) last_keycode = KEY_RIGHT;
        else last_keycode = 0;
        if (last_keycode >= KEY_UP) kbd_ready = 1;
        outb(0x20, 0x20); return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_active = 1; outb(0x20, 0x20); return; }
    if (sc == 0xAA || sc == 0xB6) { shift_active = 0; outb(0x20, 0x20); return; }
    if (sc == 0x01) { last_keycode = 0x1B; kbd_ready = 1; outb(0x20, 0x20); return; }

    if (sc < sizeof(kbd_map_normal)) {
        char c = shift_active ? kbd_map_shift[sc] : kbd_map_normal[sc];
        if (c > 0) { last_keycode = c; kbd_ready = 1; }
    }
    outb(0x20, 0x20);
}

int get_single_key() {
    kbd_ready = 0;
    while (!kbd_ready) { rand_seed++; asm volatile ("hlt"); }
    return last_keycode;
}

void readline(char* buf, int max) {
    int idx = 0;
    int hist_pos = history_count;
    for (int i = 0; i < max; i++) buf[i] = 0;

    while (1) {
        int key = get_single_key();
        if (key == '\n') { buf[idx] = 0; putchar_color('\n', COLOR_WHITE); break; }
        else if (key == '\b') { if (idx > 0) { idx--; buf[idx] = 0; putchar_color('\b', COLOR_WHITE); } }
        else if (key == KEY_UP) {
            if (hist_pos > 0) {
                hist_pos--;
                while (idx > 0) { putchar_color('\b', COLOR_WHITE); idx--; }
                strcpy(buf, cmd_history[hist_pos]);
                idx = strlen(buf); print(buf);
            }
        }
        else if (key == KEY_DOWN) {
            if (hist_pos < history_count) {
                hist_pos++;
                while (idx > 0) { putchar_color('\b', COLOR_WHITE); idx--; }
                if (hist_pos < history_count) {
                    strcpy(buf, cmd_history[hist_pos]);
                    idx = strlen(buf); print(buf);
                } else { buf[0] = 0; idx = 0; }
            }
        }
        else if (key >= ' ' && key <= '~') {
            if (idx < max - 1) { buf[idx++] = key; char s[2] = {(char)key, 0}; print(s); }
        }
    }

    if (buf[0] != 0) {
        if (history_count < HISTORY_MAX) { strcpy(cmd_history[history_count++], buf); }
        else {
            for (int i = 1; i < HISTORY_MAX; i++) strcpy(cmd_history[i-1], cmd_history[i]);
            strcpy(cmd_history[HISTORY_MAX-1], buf);
        }
    }
}

void set_idt_gate(int n, uint32_t handler, uint8_t flags) {
    idt[n].base_low = handler & 0xFFFF; idt[n].selector = 0x08; idt[n].zero = 0;
    idt[n].flags = flags; idt[n].base_high = (handler >> 16) & 0xFFFF;
}

void setup_idt() {
    extern void isr_dummy(); extern void isr_kbd(); extern void isr40();
    asm volatile ("cli");
    idt_ptr.limit = sizeof(idt) - 1; idt_ptr.base = (uint32_t)&idt;
    for (int i = 0; i < 256; i++) set_idt_gate(i, (uint32_t)isr_dummy, 0x8E);
    set_idt_gate(33, (uint32_t)isr_kbd, 0x8E);
    set_idt_gate(0x40, (uint32_t)isr40, 0xEE);
    load_idt((uint32_t)&idt_ptr);
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFD); io_wait(); outb(0xA1, 0xFF); io_wait();
    asm volatile ("sti");
}

void syscall_handler(registers_t* regs) {
    asm volatile("sti");

    if (regs->eax == 1) print((const char*)regs->ebx);
    else if (regs->eax == 2) readline((char*)regs->ebx, (int)regs->ecx);
    else if (regs->eax == 3) print_int((int)regs->ebx);
}

static void ata_wait_ready() {
    while (inb(ATA_REG_STATUS) & STATUS_BSY) io_wait();
    while (!(inb(ATA_REG_STATUS) & STATUS_DRQ)) io_wait();
}

void read_sector(uint32_t lba, uint8_t* buf) {
    outb(ATA_REG_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F)); outb(ATA_REG_SECCOUNT, 1);
    outb(ATA_REG_LBA_LO, (uint8_t)lba); outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_REG_LBA_HI, (uint8_t)(lba >> 16)); outb(ATA_REG_COMMAND, ATA_CMD_READ);
    ata_wait_ready();
    uint16_t* ptr = (uint16_t*)buf; for (int i = 0; i < 256; i++) ptr[i] = inw(ATA_REG_DATA);
}

void write_sector(uint32_t lba, const uint8_t* buf) {
    outb(ATA_REG_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F)); outb(ATA_REG_SECCOUNT, 1);
    outb(ATA_REG_LBA_LO, (uint8_t)lba); outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_REG_LBA_HI, (uint8_t)(lba >> 16)); outb(ATA_REG_COMMAND, ATA_CMD_WRITE);
    ata_wait_ready();
    uint16_t* ptr = (uint16_t*)buf; for (int i = 0; i < 256; i++) outw(ATA_REG_DATA, ptr[i]);
    outb(ATA_REG_COMMAND, 0xE7); while (inb(ATA_REG_STATUS) & STATUS_BSY) io_wait();
}

void read_inode(uint32_t idx, vfs_inode_t* inode) {
    uint8_t sector_buf[SECTOR_SIZE];
    uint32_t inodes_per_sector = SECTOR_SIZE / sizeof(vfs_inode_t);
    read_sector(INODE_START_LBA + (idx / inodes_per_sector), sector_buf);
    uint8_t* src = sector_buf + ((idx % inodes_per_sector) * sizeof(vfs_inode_t));
    for(uint32_t i=0; i<sizeof(vfs_inode_t); i++) ((uint8_t*)inode)[i] = src[i];
}

void write_inode(uint32_t idx, const vfs_inode_t* inode) {
    uint8_t sector_buf[SECTOR_SIZE];
    uint32_t inodes_per_sector = SECTOR_SIZE / sizeof(vfs_inode_t);
    uint32_t lba = INODE_START_LBA + (idx / inodes_per_sector);
    read_sector(lba, sector_buf);
    uint8_t* dest = sector_buf + ((idx % inodes_per_sector) * sizeof(vfs_inode_t));
    for(uint32_t i=0; i<sizeof(vfs_inode_t); i++) dest[i] = ((const uint8_t*)inode)[i];
    write_sector(lba, sector_buf);
}

int find_inode_by_name(uint32_t parent, const char* name) {
    vfs_inode_t node;
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &node);
        if (node.type != TYPE_FREE && node.parent_inode == parent && strcmp(node.name, name) == 0) return i;
    }
    return -1;
}

int allocate_inode(const char* name, uint32_t type, uint32_t parent) {
    int existing = find_inode_by_name(parent, name);
    if (existing != -1) return existing;

    vfs_inode_t node;
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &node);
        if (node.type == TYPE_FREE) {
            for(int j=0; j<64; j++) node.name[j] = 0;
            strcpy(node.name, name); node.type = type; node.parent_inode = parent; node.size = 0;

            uint8_t sb_buf[SECTOR_SIZE]; read_sector(SUPERBLOCK_LBA, sb_buf);
            superblock_t* sb = (superblock_t*)sb_buf;
            node.first_block = sb->next_free_block; sb->next_free_block++;
            write_sector(SUPERBLOCK_LBA, sb_buf);
            write_inode(i, &node); return i;
        }
    }
    return -1;
}

void build_absolute_path(uint32_t inode_idx, char* buffer) {
    if (inode_idx == 0) { strcpy(buffer, "/"); return; }
    uint32_t path_nodes[16]; int count = 0; uint32_t curr = inode_idx;
    while (curr != 0 && count < 16) {
        path_nodes[count++] = curr; vfs_inode_t n; read_inode(curr, &n); curr = n.parent_inode;
    }
    buffer[0] = '\0';
    for (int i = count - 1; i >= 0; i--) {
        strcat(buffer, "/"); vfs_inode_t n; read_inode(path_nodes[i], &n); strcat(buffer, n.name);
    }
}

void check_and_format_fs() {
    uint8_t sector_buf[SECTOR_SIZE]; read_sector(SUPERBLOCK_LBA, sector_buf);
    superblock_t* sb = (superblock_t*)sector_buf;
    if (sb->magic != 0x41544956) {
        sb->magic = 0x41544956; sb->total_inodes = INODE_COUNT;
        sb->data_start_block = DATA_START_LBA; sb->next_free_block = 0;
        write_sector(SUPERBLOCK_LBA, sector_buf);
        for(int i=0; i<SECTOR_SIZE; i++) sector_buf[i] = 0;
        for(uint32_t lba = INODE_START_LBA; lba < DATA_START_LBA; lba++) write_sector(lba, sector_buf);
        vfs_inode_t root; for(int j=0; j<64; j++) root.name[j] = 0;
        strcpy(root.name, "/"); root.type = TYPE_DIR; root.parent_inode = 0; root.size = 0; root.first_block = 0;
        write_inode(0, &root);
    }
}

void load_file_data(int inode_idx, uint8_t* buffer) {
    vfs_inode_t node; read_inode(inode_idx, &node);
    if (node.size == 0) return;
    uint32_t blocks = (node.size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for(uint32_t i = 0; i < blocks; i++) {
        read_sector(DATA_START_LBA + node.first_block + i, buffer + i * SECTOR_SIZE);
    }
}

void save_file_data(int inode_idx, uint8_t* buffer, uint32_t size) {
    vfs_inode_t node; read_inode(inode_idx, &node);
    uint32_t blocks_needed = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (blocks_needed == 0) blocks_needed = 1;

    uint8_t sb_buf[SECTOR_SIZE]; read_sector(SUPERBLOCK_LBA, sb_buf);
    superblock_t* sb = (superblock_t*)sb_buf;

    node.first_block = sb->next_free_block;
    sb->next_free_block += blocks_needed;
    node.size = size;

    write_sector(SUPERBLOCK_LBA, sb_buf);
    write_inode(inode_idx, &node);

    for(uint32_t i = 0; i < blocks_needed; i++) {
        write_sector(DATA_START_LBA + node.first_block + i, buffer + i * SECTOR_SIZE);
    }
}

void show_splash() {
    clear_screen();
    print_color("========================================================================\n", COLOR_CYAN);
    print_color("         _   _                       _                 \n", COLOR_BRIGHT_GREEN);
    print_color(" \\ \\    / / (_) | |                     (_)          /  \\  / __|\n", COLOR_BRIGHT_GREEN);
    print_color("  \\ \\  / /   _  | |_     _   _  _    _   _      | |  | || (___  \n", COLOR_BRIGHT_GREEN);
    print_color("   \\ \\/ /   | | | |  / _ | | '_  _ \\  | | | '_ \\    | |  | | \\_ \\ \n", COLOR_BRIGHT_GREEN);
    print_color("    \\  /    | | | |_  | (_| | | | | | | | | | | | | |  | | | ____) |\n", COLOR_BRIGHT_GREEN);
    print_color("     \\/     |_||  \\\\,_| |_| |_| |_| |_| |_| |_|  / | |_/ \n", COLOR_BRIGHT_GREEN);
    print_color("========================================================================\n", COLOR_CYAN);
    print_color("\n  Booting monolithic i386 kernel core...\n", COLOR_WHITE);
    print_color("  Mounting VitaminFS Core Sectors... [OK]\n", COLOR_GREEN);
    print_color("  Initializing Interrupt Vector Gates (IDT)... [OK]\n", COLOR_GREEN);
    print_color("\n  Press ENTER to grant control to v-shell...", COLOR_YELLOW);
    char dummy_buf[10]; readline(dummy_buf, 10);
    clear_screen();
}

void cmd_help(const char* arg) {
    if (!arg || arg[0] == 0 || strcmp(arg, "1") == 0) {
        print_color("\n--- Vitamin OS Command Help (Page 1/2) ---\n", COLOR_CYAN);
        print_color("Navigation and Directories:\n", COLOR_YELLOW);
        print("  ls          - List files and folders here\n");
        print("  cd <name>   - Enter a folder (use 'cd ..' to go back)\n");
        print("  pwd         - Show current path (where am I?)\n");
        print("  mkdir <name> - Create a new folder\n");
        print("  rmdir <name> - Remove an empty folder\n");
        print_color("\nFile Operations:\n", COLOR_YELLOW);
        print("  touch <name> - Create an empty file\n");
        print("  rm <name>    - Delete a file\n");
        print("  cp <name>    - Copy a file into kernel buffer\n");
        print("  pas          - Paste the copied file here\n");
        print("  cppas <1> <2> - Copy file <1> and immediately paste as <2>\n");
        print("  locate <name> - Find a file on disk by name\n");
        print_color("\n[ Use 'help 2' to view the next page ]\n\n", COLOR_GRAY);
    } else if (strcmp(arg, "2") == 0) {
        print_color("\n--- Vitamin OS Command Help (Page 2/2) ---\n", COLOR_CYAN);
        print_color("Text and Programs:\n", COLOR_YELLOW);
        print("  cat <name>   - Read text from a file to console\n");
        print("  edit <name>  - Open nano text editor (Now up to 256 lines!)\n");
        print("  echo <text>  - Print text (or save: echo Text > file)\n");
        print("  run <name>   - Run an external program from disk (Up to 32KB)\n");
        print("  hexdump <name>- Inspect raw binary data inside a file\n");
        print_color("\nSystem:\n", COLOR_YELLOW);
        print("  top         - Show filesystem load and transactions\n");
        print("  fastfetch   - Pretty system overview\n");
        print("  clear       - Clear terminal screen\n");
        print("  reboot      - Reboot the computer\n");
        print_color("  vitamin     - ??? (Secret command)\n", COLOR_MAGENTA);
        print_color("\n[ Use 'help 1' to view the first page ]\n\n", COLOR_GRAY);
    } else {
        print_color("Help: Page not indexed. Use 'help 1' or 'help 2'.\n", COLOR_RED);
    }
}

void cmd_vitamin() {
    print_color("\n============================================\n", COLOR_MAGENTA);
    print_color("                   Vitamin OS                 \n", COLOR_BRIGHT_GREEN);
    print_color("============================================\n", COLOR_MAGENTA);
    print("Welcome to the official company terminal!\n");
    print_color("Would you like to receive a free cyber-paste? (y/n): ", COLOR_CYAN);
    char choice[10]; readline(choice, 10);
    if (choice[0] == 'y' || choice[0] == 'Y') {
        print_color("\n[!] Dispensing: One (1) super paste...\n\n", COLOR_BRIGHT_GREEN);
    } else {
        print_color("\n[!] Dispensing cancelled.\n\n", COLOR_YELLOW);
    }
}

void cmd_fastfetch() {
    print("\n");
    print_color("      .-----------------.      ", COLOR_BRIGHT_GREEN); print_color("OS: ", COLOR_CYAN); print("Vitamin OS 1.0\n");
    print_color("     /  .-.         .-.  \\     ", COLOR_BRIGHT_GREEN); print_color("Kernel: ", COLOR_CYAN); print("Vitamin Kernel 1.0\n");
    print_color("    |  /   \\       /   \\  |    ", COLOR_BRIGHT_GREEN); print_color("Shell: ", COLOR_CYAN); print("v-shell\n");
    print_color("    |  |    \\     /    |  |    ", COLOR_BRIGHT_GREEN); print_color("Storage System: ", COLOR_CYAN); print("VitaminFS\n");
    print_color("    |  |     \\   /     |  |    ", COLOR_BRIGHT_GREEN); print_color("Active Path: ", COLOR_CYAN); print(current_path); print("\n");
    print_color("    |  \\      \\_/      /  |    ", COLOR_BRIGHT_GREEN); print_color("Clipboard State: ", COLOR_CYAN);
    if (clipboard_has_data) { print_color("Data Loaded [", COLOR_GREEN); print(clipboard_name); print_color("]\n", COLOR_GREEN); }
    else print_color("Empty\n", COLOR_GRAY);
    print_color("     \\  '-------------'  /     ", COLOR_BRIGHT_GREEN); print_color("Transactions: ", COLOR_CYAN); print_int(kernel_transactions); print(" ops\n");
    print_color("      '-----------------'      ", COLOR_BRIGHT_GREEN); print("\n");
}

void cmd_top() {
    uint32_t active_inodes = 0; uint32_t allocated_bytes = 0; vfs_inode_t node;
    for(uint32_t i=0; i<INODE_COUNT; i++) {
        read_inode(i, &node); if(node.type != TYPE_FREE) { active_inodes++; allocated_bytes += node.size; }
    }
    print_color("=== VitaminOS Dynamic Telemetry (top) ===\n", COLOR_CYAN);
    print("VFS Allocation Context: "); print_int(active_inodes); print("/"); print_int(INODE_COUNT); print(" active nodes used.\n");
    print("Storage Load: "); print_int(allocated_bytes); print(" bytes bound in dynamic blocks.\n");
    print("Kernel Transaction Clock: "); print_int(kernel_transactions); print(" loop-cycles verified.\n\n");
}

void cmd_locate(const char* arg) {
    if (!arg || arg[0] == 0) return;
    vfs_inode_t node; int found = 0; char full_path[256];
    for (uint32_t i = 1; i < INODE_COUNT; i++) {
        read_inode(i, &node);
        if (node.type != TYPE_FREE) {
            int match = 0; int nlen = strlen(node.name); int alen = strlen(arg);
            for (int j = 0; j <= nlen - alen; j++) {
                int k; for (k = 0; k < alen; k++) { if (node.name[j+k] != arg[k]) break; }
                if (k == alen) { match = 1; break; }
            }
            if (match) {
                build_absolute_path(i, full_path);
                if (node.type == TYPE_DIR) print_color("[DIR]  ", COLOR_GREEN); else print_color("[FILE] ", COLOR_WHITE);
                print(full_path); print("\n"); found = 1;
            }
        }
    }
    if (!found) print("Entry not indexed on active sectors.\n");
}

void cmd_cp(const char* arg) {
    if (!arg || arg[0] == 0) return;
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx == -1) { print_color("Source entry missing\n", COLOR_RED); return; }
    vfs_inode_t node; read_inode(idx, &node);
    if (node.type != TYPE_FILE) { print_color("Cannot buffer directory target\n", COLOR_RED); return; }

    strcpy(clipboard_name, node.name); clipboard_size = node.size;
    load_file_data(idx, clipboard_buf);
    clipboard_has_data = 1;
    print_color("File targeted and loaded into kernel staging buffer. Navigate and call 'pas'.\n", COLOR_GREEN);
}

void cmd_pas() {
    if (!clipboard_has_data) return;
    int idx = allocate_inode(clipboard_name, TYPE_FILE, current_dir_inode);
    if (idx == -1) return;
    save_file_data(idx, clipboard_buf, clipboard_size);
    print_color("Buffer synchronized with current sector. Payload deployed.\n", COLOR_BRIGHT_GREEN);
}

void cmd_cppas(char* args) {
    char* src_name; char* dest_name; split_args(args, &src_name, &dest_name);
    if (!src_name || !dest_name || src_name[0] == 0 || dest_name[0] == 0) return;
    int src_idx = find_inode_by_name(current_dir_inode, src_name);
    if (src_idx == -1) return;
    vfs_inode_t src_node; read_inode(src_idx, &src_node);

    int dest_idx = allocate_inode(dest_name, TYPE_FILE, current_dir_inode);
    if (dest_idx == -1) return;

    load_file_data(src_idx, shared_file_buf);
    save_file_data(dest_idx, shared_file_buf, src_node.size);
    print_color("Direct sector duplication completed successfully.\n", COLOR_BRIGHT_GREEN);
}

void cmd_ls() {
    vfs_inode_t node; int found = 0;
    for (uint32_t i = 0; i < INODE_COUNT; i++) {
        read_inode(i, &node);
        if (node.type != TYPE_FREE && node.parent_inode == current_dir_inode && i != current_dir_inode) {
            if (node.type == TYPE_DIR) { print_color(node.name, COLOR_GREEN); print("/"); }
            else print_color(node.name, COLOR_WHITE);
            print("  "); found = 1;
        }
    }
    if (!found) print_color("Empty directory", COLOR_GRAY);
    print("\n");
}

void cmd_cd(const char* arg) {
    if (!arg || arg[0] == 0) return;
    if (strcmp(arg, "..") == 0) {
        if (current_dir_inode != 0) {
            vfs_inode_t node; read_inode(current_dir_inode, &node); current_dir_inode = node.parent_inode;
            if (current_dir_inode == 0) strcpy(current_path, "/");
            else {
                char* last_slash = 0; for(int i=0; current_path[i]; i++) if(current_path[i] == '/') last_slash = &current_path[i];
                if (last_slash && last_slash != current_path) *last_slash = '\0'; else strcpy(current_path, "/");
            }
        } return;
    }
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx != -1) {
        vfs_inode_t node; read_inode(idx, &node);
        if (node.type == TYPE_DIR) {
            current_dir_inode = idx; if (strcmp(current_path, "/") != 0) strcat(current_path, "/");
            strcat(current_path, arg); return;
        } else { print_color("Target is a persistent data block, not directory\n", COLOR_RED); return; }
    }
    print_color("CD Error: Sector descriptor missing\n", COLOR_RED);
}

void cmd_mkdir(const char* arg) { if (!arg || arg[0] == 0) return; allocate_inode(arg, TYPE_DIR, current_dir_inode); }
void cmd_touch(const char* arg) { if (!arg || arg[0] == 0) return; allocate_inode(arg, TYPE_FILE, current_dir_inode); }

void cmd_rmdir(const char* arg) {
    if (!arg || arg[0] == 0) return;
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx != -1) {
        vfs_inode_t node; read_inode(idx, &node);
        if(node.type == TYPE_DIR) { node.type = TYPE_FREE; write_inode(idx, &node); }
    }
}
void cmd_rm(const char* arg) {
    if (!arg || arg[0] == 0) return;
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx != -1) {
        vfs_inode_t node; read_inode(idx, &node);
        if(node.type == TYPE_FILE) { node.type = TYPE_FREE; write_inode(idx, &node); }
    }
}

void cmd_cat(const char* arg) {
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx != -1) {
        vfs_inode_t node; read_inode(idx, &node);
        if (node.type == TYPE_FILE && node.size > 0) {
            load_file_data(idx, shared_file_buf);
            for (uint32_t i = 0; i < node.size; i++) putchar_color((char)shared_file_buf[i], COLOR_WHITE);
            print("\n"); return;
        }
    }
    print_color("Cat System: Object target is empty or unlinked\n", COLOR_RED);
}

void cmd_echo(char* arg) {
    if (!arg) return;
    char* redir = strchr(arg, '>');
    if (redir) {
        *redir = 0; char* filename = redir + 1; while (*filename == ' ') filename++;
        char* text = arg; while (*text == ' ') text++;
        int tlen = strlen(text); if (tlen > 0 && text[tlen-1] == ' ') text[tlen-1] = 0;
        int idx = allocate_inode(filename, TYPE_FILE, current_dir_inode);
        if (idx != -1) {
            save_file_data(idx, (uint8_t*)text, strlen(text));
        }
    } else { print(arg); print("\n"); }
}

void cmd_run(const char* arg) {
    if (!arg || arg[0] == 0) { print_color("Usage: run <filename>\n", COLOR_YELLOW); return; }
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx == -1) { print_color("Error: program descriptor not found on disk.\n", COLOR_RED); return; }
    vfs_inode_t node; read_inode(idx, &node);
    if (node.type != TYPE_FILE) { print_color("Error: target entry is not an executable file.\n", COLOR_RED); return; }

    uint8_t* app_target_buffer = (uint8_t*)0x50000;

    uint32_t blocks = (node.size + SECTOR_SIZE - 1) / SECTOR_SIZE;
    for(uint32_t i=0; i<blocks; i++) {
        read_sector(DATA_START_LBA + node.first_block + i, app_target_buffer + i*SECTOR_SIZE);
    }

    print_color("[!] Executing external x86 binary layers...\n", COLOR_YELLOW);
    typedef void (*entry_point_t)();
    entry_point_t start_app = (entry_point_t)0x50000;
    start_app();
    print_color("\n[!] Execution context terminated cleanly.\n", COLOR_BRIGHT_GREEN);
}

void cmd_hexdump(const char* arg) {
    if (!arg || arg[0] == 0) { print_color("Usage: hexdump <filename>\n", COLOR_YELLOW); return; }
    int idx = find_inode_by_name(current_dir_inode, arg);
    if (idx == -1) { print_color("File not found\n", COLOR_RED); return; }
    vfs_inode_t node; read_inode(idx, &node);
    if (node.type != TYPE_FILE || node.size == 0) { print_color("Empty/Not a file\n", COLOR_RED); return; }

    load_file_data(idx, shared_file_buf);
    uint32_t dump_size = node.size > 512 ? 512 : node.size;
    print_color("\n--- Hexdump: ", COLOR_CYAN); print(arg); print_color(" ---\n", COLOR_CYAN);

    for(uint32_t i=0; i<dump_size; i+=16) {
        print_color(" ", COLOR_GRAY);
        if (i < 16) print("0"); if (i < 256) print("0");
        print_int(i); print(" | ");

        for(int j=0; j<16; j++) {
            if (i+j < dump_size) {
                uint8_t b = shared_file_buf[i+j];
                char hex[3] = {"0123456789ABCDEF"[b >> 4], "0123456789ABCDEF"[b & 0xF], 0};
                print(hex); print(" ");
            } else print("   ");
        }
        print(" | ");
        for(int j=0; j<16; j++) {
            if (i+j < dump_size) {
                uint8_t b = shared_file_buf[i+j];
                char c[2] = {(b >= 32 && b <= 126) ? b : '.', 0};
                print(c);
            }
        }
        print("\n");
    }
    if (node.size > 512) print_color("... [Output truncated to first 512 bytes] ...\n", COLOR_GRAY);
}

void cmd_edit(const char* arg) {
    if (!arg || arg[0] == 0) { print_color("Usage: edit <filename>\n", COLOR_YELLOW); return; }
    int idx = allocate_inode(arg, TYPE_FILE, current_dir_inode);
    if (idx == -1) { print_color("VFS Access error\n", COLOR_RED); return; }

    vfs_inode_t node; read_inode(idx, &node);

    static char edit_buf[256][75];
    for(int i = 0; i < 256; i++) {
        for(int j = 0; j < 75; j++) { edit_buf[i][j] = 0; }
    }

    if (node.size > 0) {
        load_file_data(idx, shared_file_buf);
        int row = 0, col = 0;
        for (uint32_t i = 0; i < node.size && row < 256; i++) {
            if (shared_file_buf[i] == '\n') {
                row++; col = 0;
            } else if (col < 75) {
                edit_buf[row][col++] = shared_file_buf[i];
            }
        }
    }

    int cx = 0, cy = 0;
    int view_top = 0;
    int running = 1;

    clear_screen();

    while (running) {
        if (cy < view_top) view_top = cy;
        if (cy >= view_top + 20) view_top = cy - 19;

        print_color("  UW Vitamin OS nano 3.0 (Viewport Scrolling)  ", COLOR_MAGENTA);
        print_color("\n----------------------------------------------------------------------------\n", COLOR_GRAY);

        for (int y = 0; y < 20; y++) {
            int file_y = view_top + y;
            for (int x = 0; x < 75; x++) {
                char c = ' ';
                if (file_y < 256) c = edit_buf[file_y][x];
                if (c == 0) c = ' ';
                video[(y + 2) * 80 + x] = c | (COLOR_WHITE << 8);
            }
            for (int x = 75; x < 80; x++) video[(y + 2) * 80 + x] = ' ' | (COLOR_WHITE << 8);
        }

        print_color("----------------------------------------------------------------------------\n", COLOR_GRAY);
        print_color("  [ESC] Exit & Save    Arrows: Move Cursor    [Backspace] Delete", COLOR_MAGENTA);

        update_vga_cursor(cx, (cy - view_top) + 2);

        int key = get_single_key();

        if (key == 0x1B) { running = 0; }
        else if (key == KEY_UP) { if (cy > 0) cy--; }
        else if (key == KEY_DOWN) { if (cy < 255) cy++; }
        else if (key == KEY_LEFT) { if (cx > 0) cx--; }
        else if (key == KEY_RIGHT) { if (cx < 74) cx++; }
        else if (key == '\b') {
            if (cx > 0) {
                for (int i = cx - 1; i < 74; i++) edit_buf[cy][i] = edit_buf[cy][i+1];
                edit_buf[cy][74] = 0; cx--;
            } else if (cy > 0) {
                int old_len = 0; while (old_len < 75 && edit_buf[cy][old_len] != 0) old_len++;
                int new_len = 0; while (new_len < 75 && edit_buf[cy-1][new_len] != 0) new_len++;
                if (new_len + old_len < 75) {
                    for (int i = 0; i < old_len; i++) edit_buf[cy-1][new_len + i] = edit_buf[cy][i];
                    edit_buf[cy-1][new_len + old_len] = 0;
                }
                for (int i = 0; i < 75; i++) edit_buf[cy][i] = 0;
                for (int y = cy; y < 255; y++) {
                    for (int x = 0; x < 75; x++) edit_buf[y][x] = edit_buf[y+1][x];
                }
                for (int x = 0; x < 75; x++) edit_buf[255][x] = 0;
                cy--; cx = new_len;
            }
        } else if (key == '\n') {
            if (cy < 255) {
                for (int y = 255; y > cy + 1; y--) {
                    for (int x = 0; x < 75; x++) edit_buf[y][x] = edit_buf[y-1][x];
                }
                for (int x = 0; x < 75; x++) edit_buf[cy+1][x] = 0;
                int tail_start = cx;
                for (int x = tail_start; x < 75; x++) {
                    edit_buf[cy+1][x - tail_start] = edit_buf[cy][x];
                    edit_buf[cy][x] = 0;
                }
                cy++; cx = 0;
            }
        } else if (key >= ' ' && key <= '~') {
            if (cx < 75) {
                for (int i = 74; i > cx; i--) edit_buf[cy][i] = edit_buf[cy][i-1];
                edit_buf[cy][cx] = (char)key;
                if (cx < 74) cx++;
            }
        }
    }

    int last_row = 0;
    for(int y = 255; y >= 0; y--) {
        int has_char = 0;
        for(int x = 0; x < 75; x++) if(edit_buf[y][x]) has_char = 1;
        if(has_char) { last_row = y; break; }
    }

    int pos = 0;
    for (int y = 0; y <= last_row && pos < MAX_FILE_SIZE - 1; y++) {
        int line_len = 0;
        for (int x = 74; x >= 0; x--) if (edit_buf[y][x] != 0) { line_len = x + 1; break; }
        for (int x = 0; x < line_len && pos < MAX_FILE_SIZE - 1; x++) shared_file_buf[pos++] = edit_buf[y][x];
        if (y < last_row && pos < MAX_FILE_SIZE - 1) shared_file_buf[pos++] = '\n';
    }

    save_file_data(idx, shared_file_buf, pos);
    clear_screen();
    cmd_fastfetch();
}

void shell() {
    cmd_fastfetch();
    while (1) {
        print_color("vitamin-os:", COLOR_CYAN); print(current_path); print(" $ ");
        readline(cmd_buffer, MAX_CMD_LEN);
        kernel_transactions++;

        int len = strlen(cmd_buffer);
        while (len > 0 && (cmd_buffer[len-1] == '\n' || cmd_buffer[len-1] == '\r' || cmd_buffer[len-1] == ' ')) {
            cmd_buffer[len-1] = '\0'; len--;
        }
        if (!cmd_buffer[0]) continue;
        char* cmd = cmd_buffer; char* arg = cmd_buffer;
        while (*arg && *arg != ' ') arg++; if (*arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }

        if (strcmp(cmd, "help") == 0) cmd_help(arg);
        else if (strcmp(cmd, "fastfetch") == 0) cmd_fastfetch();
        else if (strcmp(cmd, "top") == 0) cmd_top();
        else if (strcmp(cmd, "ls") == 0) cmd_ls();
        else if (strcmp(cmd, "cd") == 0) cmd_cd(arg);
        else if (strcmp(cmd, "pwd") == 0) { print(current_path); print("\n"); }
        else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(arg);
        else if (strcmp(cmd, "rmdir") == 0) cmd_rmdir(arg);
        else if (strcmp(cmd, "touch") == 0) cmd_touch(arg);
        else if (strcmp(cmd, "rm") == 0) cmd_rm(arg);
        else if (strcmp(cmd, "cp") == 0) cmd_cp(arg);
        else if (strcmp(cmd, "pas") == 0) cmd_pas();
        else if (strcmp(cmd, "cppas") == 0) cmd_cppas(arg);
        else if (strcmp(cmd, "locate") == 0) cmd_locate(arg);
        else if (strcmp(cmd, "cat") == 0) cmd_cat(arg);
        else if (strcmp(cmd, "echo") == 0) cmd_echo(arg);
        else if (strcmp(cmd, "edit") == 0) cmd_edit(arg);
        else if (strcmp(cmd, "run") == 0) cmd_run(arg);
        else if (strcmp(cmd, "hexdump") == 0) cmd_hexdump(arg);
        else if (strcmp(cmd, "vitamin") == 0) cmd_vitamin();
        else if (strcmp(cmd, "clear") == 0) { clear_screen(); cmd_fastfetch(); }
        else if (strcmp(cmd, "reboot") == 0) { outb(0x64, 0xFE); }
        else print_color("Unknown command\n", COLOR_RED);
    }
}

void kernel_main() {
    clear_screen();
    setup_idt();
    check_and_format_fs();
    show_splash();
    shell();
}
