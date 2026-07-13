// SPDX-License-Identifier: Apache-2.0
/*
 * Recovery update-binary for Xiaomi Fire GKI 6.6 GPT migration.
 *
 * It shrinks vendor_boot_a and vendor_boot_b from the stock 64 MiB GPT entries
 * to 8 MiB, leaving partition starts and all following partitions untouched.
 */

#ifdef HOST_TEST
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;

static int ui_fd = 1;

static long sys_openat(long dirfd, const char *path, long flags, long mode) {
    return openat((int)dirfd, path, (int)flags, (mode_t)mode);
}
static long sys_close(long fd) { return close((int)fd); }
static long sys_read(long fd, void *buf, unsigned long count) {
    return read((int)fd, buf, count);
}
static long sys_write(long fd, const void *buf, unsigned long count) {
    return write((int)fd, buf, count);
}
static s64 sys_lseek(long fd, s64 off, long whence) {
    return lseek((int)fd, (off_t)off, (int)whence);
}
static long sys_fsync(long fd) { return fsync((int)fd); }
static long sys_ioctl(long fd, unsigned long req, unsigned long arg) {
    return ioctl((int)fd, req, (void *)arg);
}
#else
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;

static int ui_fd = 1;

#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define SEEK_SET 0

static long syscall1(long n, long a0) {
    register long x0 __asm__("x0") = a0;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static long syscall3(long n, long a0, long a1, long a2) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8)
                     : "memory");
    return x0;
}

static long syscall4(long n, long a0, long a1, long a2, long a3) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x8 __asm__("x8") = n;
    __asm__ volatile("svc #0" : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

static long sys_openat(long dirfd, const char *path, long flags, long mode) {
    return syscall4(56, dirfd, (long)path, flags, mode);
}
static long sys_close(long fd) { return syscall1(57, fd); }
static long sys_read(long fd, void *buf, unsigned long count) {
    return syscall3(63, fd, (long)buf, count);
}
static long sys_write(long fd, const void *buf, unsigned long count) {
    return syscall3(64, fd, (long)buf, count);
}
static s64 sys_lseek(long fd, s64 off, long whence) {
    return syscall3(62, fd, (long)off, whence);
}
static long sys_fsync(long fd) { return syscall1(82, fd); }
static long sys_ioctl(long fd, unsigned long req, unsigned long arg) {
    return syscall3(29, fd, req, arg);
}
static void sys_exit(long code) {
    syscall1(94, code);
    for (;;) {
    }
}
#endif

#define SECTOR_SIZE 512ULL
#define MAX_GPT_BYTES 131072U
#define BACKUP_BYTES (1024U * 1024U)
#define BLKRRPART 0x125f

#define BOOT_A_FIRST 0xff910ULL
#define BOOT_A_LAST 0x13f90fULL
#define VENDOR_BOOT_A_FIRST 0x13f910ULL
#define VENDOR_BOOT_A_OLD_LAST 0x15f90fULL
#define VENDOR_BOOT_A_NEW_LAST 0x14390fULL
#define DTBO_A_FIRST 0x15f910ULL

#define BOOT_B_FIRST 0x20dd10ULL
#define BOOT_B_LAST 0x24dd0fULL
#define VENDOR_BOOT_B_FIRST 0x24dd10ULL
#define VENDOR_BOOT_B_OLD_LAST 0x26dd0fULL
#define VENDOR_BOOT_B_NEW_LAST 0x251d0fULL
#define DTBO_B_FIRST 0x26dd10ULL

static u8 gpt_entries[MAX_GPT_BYTES];
static u8 header[SECTOR_SIZE];
static u8 backup_header[SECTOR_SIZE];
static u8 io_buf[4096];

static unsigned long cstr_len(const char *s) {
    unsigned long n = 0;
    while (s[n])
        n++;
    return n;
}

static void print_raw_fd(long fd, const char *s) {
    sys_write(fd, s, cstr_len(s));
}

static void print_raw(const char *s) {
    print_raw_fd(ui_fd, s);
    if (ui_fd != 1)
        print_raw_fd(1, s);
}

static void ui_print(const char *s) {
#ifdef HOST_TEST
    print_raw(s);
    print_raw("\n");
#else
    print_raw("ui_print ");
    print_raw(s);
    print_raw("\nui_print\n");
#endif
}

static void print_hex_u64(u64 value) {
    char buf[19];
    int pos = 18;
    buf[pos] = 0;
    if (!value) {
        print_raw("0x0");
        return;
    }
    while (value && pos > 2) {
        static const char h[] = "0123456789abcdef";
        buf[--pos] = h[value & 0xf];
        value >>= 4;
    }
    buf[--pos] = 'x';
    buf[--pos] = '0';
    print_raw(&buf[pos]);
}

static int parse_fd(const char *s) {
    int v = 0;
    if (!s)
        return 1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v > 0 ? v : 1;
}

static u32 get_le32(const u8 *p) {
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u64 get_le64(const u8 *p) {
    return ((u64)get_le32(p)) | ((u64)get_le32(p + 4) << 32);
}

static void put_le32(u8 *p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void put_le64(u8 *p, u64 v) {
    put_le32(p, (u32)v);
    put_le32(p + 4, (u32)(v >> 32));
}

static int mem_eq(const u8 *a, const char *b, unsigned long len) {
    unsigned long i;
    for (i = 0; i < len; i++) {
        if (a[i] != (u8)b[i])
            return 0;
    }
    return 1;
}

static u32 crc32_ieee(const u8 *data, u32 len) {
    u32 crc = 0xffffffffU;
    u32 i;
    int bit;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static u32 crc32_gpt_header(const u8 *h, u32 len) {
    u32 crc = 0xffffffffU;
    u32 i;
    int bit;
    for (i = 0; i < len; i++) {
        u8 byte = (i >= 16 && i < 20) ? 0 : h[i];
        crc ^= byte;
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static int read_exact(long fd, u64 off, void *buf, u64 len) {
    u8 *p = (u8 *)buf;
    if (sys_lseek(fd, (s64)off, SEEK_SET) < 0)
        return -1;
    while (len) {
        long r = sys_read(fd, p, (unsigned long)len);
        if (r <= 0)
            return -1;
        p += r;
        len -= (u64)r;
    }
    return 0;
}

static int write_exact(long fd, u64 off, const void *buf, u64 len) {
    const u8 *p = (const u8 *)buf;
    if (sys_lseek(fd, (s64)off, SEEK_SET) < 0)
        return -1;
    while (len) {
        long w = sys_write(fd, p, (unsigned long)len);
        if (w <= 0)
            return -1;
        p += w;
        len -= (u64)w;
    }
    return 0;
}

static int copy_range(long src, long dst, u64 off, u64 len) {
    if (sys_lseek(src, (s64)off, SEEK_SET) < 0)
        return -1;
    while (len) {
        unsigned long chunk = len > sizeof(io_buf) ? sizeof(io_buf) :
                                                     (unsigned long)len;
        long r = sys_read(src, io_buf, chunk);
        if (r <= 0)
            return -1;
        if (sys_write(dst, io_buf, (unsigned long)r) != r)
            return -1;
        len -= (u64)r;
    }
    return 0;
}

static int name_eq(const u8 *entry, const char *name) {
    unsigned long i;
    const u8 *n = entry + 56;
    for (i = 0; name[i]; i++) {
        if (i >= 36 || n[i * 2] != (u8)name[i] || n[i * 2 + 1] != 0)
            return 0;
    }
    return i < 36 && n[i * 2] == 0 && n[i * 2 + 1] == 0;
}

static int check_fixed_part(const u8 *entry, const char *name, u64 first,
                            u64 last) {
    u64 got_first = get_le64(entry + 32);
    u64 got_last = get_le64(entry + 40);
    if (got_first == first && got_last == last)
        return 0;
    ui_print("unexpected fixed partition geometry");
    print_raw("ui_print ");
    print_raw(name);
    print_raw(" got first=");
    print_hex_u64(got_first);
    print_raw(" last=");
    print_hex_u64(got_last);
    print_raw("\nui_print\n");
    return -1;
}

static int patch_vendor_part(u8 *entry, const char *name, u64 first,
                             u64 old_last, u64 new_last, int *changed) {
    u64 got_first = get_le64(entry + 32);
    u64 got_last = get_le64(entry + 40);
    if (got_first != first) {
        ui_print("unexpected vendor_boot start LBA");
        return -1;
    }
    if (got_last == new_last) {
        print_raw("ui_print already patched: ");
        print_raw(name);
        print_raw("\nui_print\n");
        return 0;
    }
    if (got_last != old_last) {
        ui_print("unexpected vendor_boot size; refusing to patch");
        return -1;
    }
    put_le64(entry + 40, new_last);
    *changed = 1;
    print_raw("ui_print patched: ");
    print_raw(name);
    print_raw(" last_lba ");
    print_hex_u64(old_last);
    print_raw(" -> ");
    print_hex_u64(new_last);
    print_raw("\nui_print\n");
    return 0;
}

static int patch_entries(u32 count, u32 size, int *changed) {
    int have_boot_a = 0, have_boot_b = 0, have_vba = 0, have_vbb = 0;
    int have_dtbo_a = 0, have_dtbo_b = 0;
    u32 i;

    for (i = 0; i < count; i++) {
        u8 *entry = gpt_entries + (u64)i * size;
        if (name_eq(entry, "boot_a")) {
            have_boot_a = 1;
            if (check_fixed_part(entry, "boot_a", BOOT_A_FIRST, BOOT_A_LAST))
                return -1;
        } else if (name_eq(entry, "boot_b")) {
            have_boot_b = 1;
            if (check_fixed_part(entry, "boot_b", BOOT_B_FIRST, BOOT_B_LAST))
                return -1;
        } else if (name_eq(entry, "vendor_boot_a")) {
            have_vba = 1;
            if (patch_vendor_part(entry, "vendor_boot_a", VENDOR_BOOT_A_FIRST,
                                  VENDOR_BOOT_A_OLD_LAST,
                                  VENDOR_BOOT_A_NEW_LAST, changed))
                return -1;
        } else if (name_eq(entry, "vendor_boot_b")) {
            have_vbb = 1;
            if (patch_vendor_part(entry, "vendor_boot_b", VENDOR_BOOT_B_FIRST,
                                  VENDOR_BOOT_B_OLD_LAST,
                                  VENDOR_BOOT_B_NEW_LAST, changed))
                return -1;
        } else if (name_eq(entry, "dtbo_a")) {
            have_dtbo_a = 1;
            if (get_le64(entry + 32) != DTBO_A_FIRST)
                return -1;
        } else if (name_eq(entry, "dtbo_b")) {
            have_dtbo_b = 1;
            if (get_le64(entry + 32) != DTBO_B_FIRST)
                return -1;
        }
    }

    if (!have_boot_a || !have_boot_b || !have_vba || !have_vbb ||
        !have_dtbo_a || !have_dtbo_b) {
        ui_print("required Fire A/B partition names were not found");
        return -1;
    }
    return 0;
}

static int validate_header(const u8 *h, const char *which) {
    u32 header_size;
    u32 expected_crc;
    u32 actual_crc;
    if (!mem_eq(h, "EFI PART", 8)) {
        print_raw("ui_print bad GPT signature in ");
        print_raw(which);
        print_raw("\nui_print\n");
        return -1;
    }
    header_size = get_le32(h + 12);
    if (header_size < 92 || header_size > SECTOR_SIZE) {
        ui_print("unsupported GPT header size");
        return -1;
    }
    expected_crc = get_le32(h + 16);
    actual_crc = crc32_gpt_header(h, header_size);
    if (actual_crc != expected_crc) {
        print_raw("ui_print bad GPT header CRC in ");
        print_raw(which);
        print_raw("\nui_print\n");
        return -1;
    }
    return 0;
}

static void refresh_header_crc(u8 *h) {
    u32 header_size = get_le32(h + 12);
    put_le32(h + 16, 0);
    put_le32(h + 16, crc32_ieee(h, header_size));
}

static int write_backup(long disk, u64 backup_lba) {
    static const char *paths[] = {
        "/sdcard/Fire-GKI66-vendor_boot-8M-gpt-backup.bin",
        "/data/media/0/Fire-GKI66-vendor_boot-8M-gpt-backup.bin",
        "/cache/Fire-GKI66-vendor_boot-8M-gpt-backup.bin",
        "/tmp/Fire-GKI66-vendor_boot-8M-gpt-backup.bin",
    };
    u64 disk_bytes = (backup_lba + 1ULL) * SECTOR_SIZE;
    unsigned i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        long out = sys_openat(AT_FDCWD, paths[i], O_WRONLY | O_CREAT | O_TRUNC,
                              0600);
        if (out < 0)
            continue;
        if (copy_range(disk, out, 0, BACKUP_BYTES) == 0 &&
            copy_range(disk, out, disk_bytes - BACKUP_BYTES, BACKUP_BYTES) == 0) {
            sys_fsync(out);
            sys_close(out);
            print_raw("ui_print GPT backup saved: ");
            print_raw(paths[i]);
            print_raw("\nui_print\n");
            return 0;
        }
        sys_close(out);
    }

    ui_print("failed to save GPT backup; refusing to repartition");
    return -1;
}

static int repartition(const char *disk_path) {
    long disk;
    u64 entry_lba, backup_lba, backup_entry_lba;
    u32 count, size, entries_bytes, entries_crc;
    int changed = 0;

    ui_print("Fire GKI 6.6 vendor_boot GPT migration");
    disk = sys_openat(AT_FDCWD, disk_path, O_RDWR, 0);
    if (disk < 0) {
        ui_print("failed to open /dev/block/mmcblk0");
        return 1;
    }

    if (read_exact(disk, SECTOR_SIZE, header, sizeof(header)) ||
        validate_header(header, "primary")) {
        sys_close(disk);
        return 1;
    }

    backup_lba = get_le64(header + 32);
    entry_lba = get_le64(header + 72);
    count = get_le32(header + 80);
    size = get_le32(header + 84);
    entries_bytes = count * size;
    if (!count || size < 128 || size > 256 ||
        count > MAX_GPT_BYTES / size || entries_bytes > MAX_GPT_BYTES) {
        ui_print("unsupported GPT partition entry layout");
        sys_close(disk);
        return 1;
    }

    if (read_exact(disk, backup_lba * SECTOR_SIZE, backup_header,
                   sizeof(backup_header)) ||
        validate_header(backup_header, "backup")) {
        sys_close(disk);
        return 1;
    }
    backup_entry_lba = get_le64(backup_header + 72);

    if (write_backup(disk, backup_lba)) {
        sys_close(disk);
        return 1;
    }

    if (read_exact(disk, entry_lba * SECTOR_SIZE, gpt_entries, entries_bytes)) {
        ui_print("failed to read GPT entries");
        sys_close(disk);
        return 1;
    }
    if (crc32_ieee(gpt_entries, entries_bytes) != get_le32(header + 88)) {
        ui_print("bad primary GPT entries CRC; refusing to patch");
        sys_close(disk);
        return 1;
    }

    if (patch_entries(count, size, &changed)) {
        sys_close(disk);
        return 1;
    }

    if (!changed) {
        ui_print("GPT already has 8 MiB vendor_boot_a/b");
        sys_close(disk);
        return 0;
    }

    entries_crc = crc32_ieee(gpt_entries, entries_bytes);
    put_le32(header + 88, entries_crc);
    put_le32(backup_header + 88, entries_crc);
    refresh_header_crc(header);
    refresh_header_crc(backup_header);

    if (write_exact(disk, backup_entry_lba * SECTOR_SIZE, gpt_entries,
                    entries_bytes) ||
        write_exact(disk, backup_lba * SECTOR_SIZE, backup_header,
                    sizeof(backup_header)) ||
        write_exact(disk, entry_lba * SECTOR_SIZE, gpt_entries,
                    entries_bytes) ||
        write_exact(disk, SECTOR_SIZE, header, sizeof(header))) {
        ui_print("failed to write updated GPT");
        sys_close(disk);
        return 1;
    }

    sys_fsync(disk);
    if (sys_ioctl(disk, BLKRRPART, 0) < 0)
        ui_print("kernel kept old partition table until reboot");
    sys_close(disk);
    ui_print("done: vendor_boot_a/b GPT size is now 8 MiB");
    return 0;
}

static int app_main(int argc, char **argv) {
    const char *disk_path = "/dev/block/mmcblk0";
    if (argc >= 3)
        ui_fd = parse_fd(argv[2]);
#ifdef HOST_TEST
    if (argc >= 2)
        disk_path = argv[1];
#endif
    return repartition(disk_path);
}

#ifdef HOST_TEST
int main(int argc, char **argv) {
    return app_main(argc, argv);
}
#else
void _start(void) {
    long *sp;
    int argc;
    char **argv;
    int rc;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    argc = (int)*sp++;
    argv = (char **)sp;
    rc = app_main(argc, argv);
    sys_exit(rc);
}
#endif
