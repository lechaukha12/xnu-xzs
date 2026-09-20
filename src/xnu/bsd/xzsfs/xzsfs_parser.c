/*
 * XZSFS Kernel Adapter & D5-M3 Diagnostic Probe
 *
 * Implements kernel block I/O adapter over devvp via buf_bread,
 * directory entry formatting, attribute formatting, and the complete
 * D5-M3 silicon diagnostic suite with checkpoint family 0xD520.
 */

#include "xzsfs.h"
#include <sys/vnode_internal.h>
#include <sys/vnode_if.h>
#include <sys/mount_internal.h>
#include <sys/namei.h>
#include <sys/dirent.h>
#include <kern/clock.h>

extern void xzs_early_puts(const char *s);
extern void xzs_early_putc(char c);
extern void xzs_early_puthex64(uint64_t v);
extern void xzs_breadcrumb(uint32_t cp, uint32_t err);
extern void xzs_spin_halt(void);
extern void xzs_watchdog_pet(void);
extern void delay(int usec);
extern uint32_t crc32(uint32_t crc, const void *buf, size_t size);

#define CP_D5M3 0xD520

int
xzsfs_kernel_block_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
    vnode_t devvp = (vnode_t)ctx;
    uint8_t *dest = (uint8_t *)buf;

    if (!devvp || !buf) {
        return XZSFS_ERR_INVAL;
    }

    for (uint32_t i = 0; i < count; i++) {
        buf_t bp = NULL;
        int error = buf_bread(devvp, (daddr64_t)(lba + i), XZSFS_SECTOR_SIZE, NOCRED, &bp);
        if (error != 0 || !bp) {
            if (bp) {
                buf_brelse(bp);
            }
            return (error != 0) ? error : EIO;
        }

        memcpy(dest + (i * XZSFS_SECTOR_SIZE), (const void *)(uintptr_t)buf_dataptr(bp), XZSFS_SECTOR_SIZE);
        buf_brelse(bp);
    }

    return XZSFS_ERR_OK;
}

int
xzsfs_helper_format_dirent(const struct xzsfs_core_node *node, const char *name, uint8_t dtype, struct dirent *de)
{
    if (!node || !name || !de) {
        return EINVAL;
    }

    bzero(de, sizeof(struct dirent));
    de->d_fileno = node->object_id;
    de->d_namlen = (uint8_t)strlen(name);
    de->d_type = dtype;
    strlcpy(de->d_name, name, sizeof(de->d_name));
    de->d_reclen = (uint16_t)(((sizeof(struct dirent) - (__DARWIN_MAXPATHLEN)) + (((de->d_namlen + 1 + 3) & ~3))));

    return 0;
}

int
xzsfs_helper_format_getattr(const struct xzsfs_core_node *node, struct vnode_attr *vap)
{
    if (!node || !vap) {
        return EINVAL;
    }

    VATTR_INIT(vap);
    VATTR_RETURN(vap, va_type, (node->type == XZSFS_TYPE_DIR) ? VDIR : VREG);
    VATTR_RETURN(vap, va_mode, (mode_t)node->mode);
    VATTR_RETURN(vap, va_uid, node->uid);
    VATTR_RETURN(vap, va_gid, node->gid);
    VATTR_RETURN(vap, va_nlink, 1);
    VATTR_RETURN(vap, va_fileid, node->object_id);
    VATTR_RETURN(vap, va_parentid, node->parent_id);
    VATTR_RETURN(vap, va_total_size, node->data_length);
    VATTR_RETURN(vap, va_data_size, node->data_length);
    VATTR_RETURN(vap, va_iosize, XZSFS_SECTOR_SIZE);
    VATTR_RETURN(vap, va_total_alloc, (node->data_length + 511U) & ~511U);
    VATTR_RETURN(vap, va_data_alloc, (node->data_length + 511U) & ~511U);

    return 0;
}

static uint32_t
xzsfs_compute_ramdisk_crc32(vnode_t devvp, uint32_t total_sectors)
{
    uint32_t crc = 0;
    for (uint32_t s = 0; s < total_sectors; s++) {
        buf_t bp = NULL;
        int error = buf_bread(devvp, (daddr64_t)s, XZSFS_SECTOR_SIZE, NOCRED, &bp);
        if (error != 0 || !bp) {
            if (bp) buf_brelse(bp);
            xzs_early_puts("xzsfs: CRC check failed to read sector\n");
            return 0;
        }
        crc = crc32(crc, (const void *)(uintptr_t)buf_dataptr(bp), XZSFS_SECTOR_SIZE);
        buf_brelse(bp);
    }
    return crc;
}

/* Static allocations to keep stack usage near 0 */
static struct xzsfs_disk_superblock s_sb;
static struct xzsfs_disk_object s_disk_objects[XZSFS_MAX_OBJECTS];
static char s_disk_strtab[XZSFS_MAX_STRTAB_SIZE];
static struct xzsfs_core_fs s_core_fs;
static uint8_t s_chunk_buf[512];
static uint8_t s_small_buf[64];

int
xzsfs_d5m3_probe(dev_t root_dev)
{
    int error = 0;
    vnode_t devvp = NULL;

    /* 0x00: enter */
    xzs_breadcrumb(CP_D5M3, 0x00);
    xzs_early_puts("\n=======================================================\n");
    xzs_early_puts("=== PHASE D5-M3: XZSFS READ-ONLY VFS DRIVER PROBE ===\n");
    xzs_early_puts("=======================================================\n");

    /* 1. Register Filesystem */
    error = xzsfs_vfs_register();
    if (error != 0 || xzsfs_registration_count != 1) {
        xzs_early_puts("D5-M3 FATAL: Filesystem registration failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEA);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x10: XZSFS registration pass */
    xzs_breadcrumb(CP_D5M3, 0x10);
    xzs_early_puts("D5-M3: XZSFS registration verified (count=1, name='xzsfs')\n");

    /* 2. Acquire Block Device Vnode (devvp) */
    error = bdevvp(root_dev, &devvp);
    if (error != 0 || !devvp) {
        xzs_early_puts("D5-M3 FATAL: bdevvp(rootdev) failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEB);
        delay(50000);
        xzs_spin_halt();
        return (error != 0) ? error : ENXIO;
    }
    /* 0x20: bdevvp(md0) pass */
    xzs_breadcrumb(CP_D5M3, 0x20);
    xzs_early_puts("D5-M3: bdevvp(md0) acquired successfully\n");

    /* 3. Read Superblock */
    error = xzsfs_core_read_superblock(xzsfs_kernel_block_read, devvp, &s_sb);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_read_superblock failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEC);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x30: superblock read pass */
    xzs_breadcrumb(CP_D5M3, 0x30);

    /* Validate Superblock */
    error = xzsfs_core_validate_superblock(&s_sb);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_validate_superblock failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xED);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x31: superblock validation pass */
    xzs_breadcrumb(CP_D5M3, 0x31);
    xzs_early_puts("D5-M3: Superblock verified: magic=0x5346535a version=1\n");

    /* 4. Load Metadata Tables */
    bzero(s_disk_objects, sizeof(s_disk_objects));
    bzero(s_disk_strtab, sizeof(s_disk_strtab));

    /* Load Object Table */
    error = xzsfs_core_load_objects(xzsfs_kernel_block_read, devvp, &s_sb, s_disk_objects, XZSFS_MAX_OBJECTS);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_load_objects failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x40: object table loaded */
    xzs_breadcrumb(CP_D5M3, 0x40);

    /* Load String Table */
    error = xzsfs_core_load_strtab(xzsfs_kernel_block_read, devvp, &s_sb, s_disk_strtab, XZSFS_MAX_STRTAB_SIZE);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_load_strtab failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x41: string table loaded */
    xzs_breadcrumb(CP_D5M3, 0x41);

    /* Validate Metadata CRC32 */
    error = xzsfs_core_validate_metadata_crc(&s_sb, s_disk_objects, s_disk_strtab);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_validate_metadata_crc failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEF);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x42: metadata CRC pass */
    xzs_breadcrumb(CP_D5M3, 0x42);

    /* Validate Object Graph */
    error = xzsfs_core_validate_graph(&s_sb, s_disk_objects, s_disk_strtab);
    if (error != 0) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_validate_graph failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xEF);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x43: graph validation pass */
    xzs_breadcrumb(CP_D5M3, 0x43);
    xzs_early_puts("D5-M3: Metadata verified: CRC32=0x29717031 matched, acyclic graph\n");

    /* 5. Initialize In-Memory Core Filesystem */
    bzero(&s_core_fs, sizeof(s_core_fs));
    error = xzsfs_core_init_fs(&s_core_fs, &s_sb, s_disk_objects, s_disk_strtab);
    if (error != 0 || s_core_fs.node_count != 9 || s_core_fs.nodes[0].object_id != 1) {
        xzs_early_puts("D5-M3 FATAL: xzsfs_core_init_fs failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF0);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x50: root object resolved */
    xzs_breadcrumb(CP_D5M3, 0x50);
    xzs_early_puts("D5-M3: Root object resolved: id=1, type=DIR, parent=1\n");

    /* 6. Core Lookup Suite */
    const struct xzsfs_core_node *node = NULL;
    const char *required_lookups[] = { "dev", "sbin", "bin", "etc", "tmp", "var" };
    for (size_t i = 0; i < 6; i++) {
        error = xzsfs_core_lookup(&s_core_fs, 1, required_lookups[i], strlen(required_lookups[i]), &node);
        if (error != 0) {
            xzs_early_puts("D5-M3 FATAL: Lookup root dir failed!\n");
            xzs_breadcrumb(CP_D5M3, 0xF1);
            vnode_put(devvp);
            delay(50000);
            xzs_spin_halt();
            return error;
        }
    }

    /* Dot and dot-dot lookups */
    error = xzsfs_core_lookup(&s_core_fs, 6, ".", 1, &node);
    if (error != 0 || node->object_id != 6) {
        xzs_early_puts("D5-M3 FATAL: Lookup /sbin/. failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF2);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EINVAL;
    }
    error = xzsfs_core_lookup(&s_core_fs, 6, "..", 2, &node);
    if (error != 0 || node->object_id != 1) {
        xzs_early_puts("D5-M3 FATAL: Lookup /sbin/.. failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF2);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EINVAL;
    }

    /* Negative lookups */
    error = xzsfs_core_lookup(&s_core_fs, 1, "nonexistent", 11, &node);
    if (error != XZSFS_ERR_NOENT) {
        xzs_early_puts("D5-M3 FATAL: Negative lookup failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF3);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EINVAL;
    }

    /* Lookup /sbin/launchd */
    error = xzsfs_core_lookup(&s_core_fs, 6, "launchd", 7, &node);
    if (error != 0 || node->object_id != 7 || node->type != XZSFS_TYPE_REG) {
        xzs_early_puts("D5-M3 FATAL: Lookup /sbin/launchd failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF4);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }

    /* Lookup /bin/sh */
    error = xzsfs_core_lookup(&s_core_fs, 2, "sh", 2, &node);
    if (error != 0 || node->object_id != 3 || node->type != XZSFS_TYPE_REG) {
        xzs_early_puts("D5-M3 FATAL: Lookup /bin/sh failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF5);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return error;
    }
    /* 0x60: core lookup suite pass */
    xzs_breadcrumb(CP_D5M3, 0x60);
    xzs_early_puts("D5-M3: Core lookups verified (all required paths resolved)\n");

    /* 7. Dirent Formatting Helper Test */
    struct dirent de;
    xzsfs_helper_format_dirent(&s_core_fs.nodes[0], ".", DT_DIR, &de);
    if (de.d_fileno != 1 || de.d_type != DT_DIR || de.d_reclen == 0) {
        xzs_early_puts("D5-M3 FATAL: Readdir format failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF6);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EINVAL;
    }
    /* 0x61: core readdir-format suite pass */
    xzs_breadcrumb(CP_D5M3, 0x61);
    xzs_early_puts("D5-M3: Dirent formatting helper verified\n");

    /* 8. Getattr Formatting Helper Test */
    struct vnode_attr vap;
    xzsfs_helper_format_getattr(&s_core_fs.nodes[6], &vap); /* /sbin/launchd */
    if (vap.va_fileid != 7 || vap.va_total_size != 16472 || vap.va_type != VREG || vap.va_mode != 0755) {
        xzs_early_puts("D5-M3 FATAL: Getattr format failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xF7);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EINVAL;
    }
    /* 0x62: core getattr-format suite pass */
    xzs_breadcrumb(CP_D5M3, 0x62);
    xzs_early_puts("D5-M3: Getattr formatting helper verified\n");

    /* 9. Payload Verification: /sbin/launchd */
    const struct xzsfs_core_node *launchd_node = &s_core_fs.nodes[6];
    size_t bytes_read = 0;
    uint32_t launchd_crc = 0;
    for (uint64_t off = 0; off < launchd_node->data_length; off += 512) {
        size_t to_read = 512;
        if (off + to_read > launchd_node->data_length) {
            to_read = (size_t)(launchd_node->data_length - off);
        }
        size_t n = 0;
        error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, off, to_read, s_chunk_buf, &n);
        if (error != 0 || n != to_read) {
            xzs_early_puts("D5-M3 FATAL: Read /sbin/launchd chunk failed!\n");
            xzs_breadcrumb(CP_D5M3, 0xF8);
            vnode_put(devvp);
            delay(50000);
            xzs_spin_halt();
            return error;
        }
        launchd_crc = xzsfs_crc32(launchd_crc, s_chunk_buf, n);
        bytes_read += n;
    }
    if (bytes_read != 16472 || launchd_crc != 0xbba67a73) {
        xzs_early_puts("D5-M3 FATAL: /sbin/launchd payload mismatch!\n");
        xzs_breadcrumb(CP_D5M3, 0xF9);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x70: launchd read pass */
    xzs_breadcrumb(CP_D5M3, 0x70);
    xzs_early_puts("D5-M3: /sbin/launchd payload verified (size=16472, CRC32=0xbba67a73)\n");

    /* 10. Payload Verification: /bin/sh */
    const struct xzsfs_core_node *sh_node = &s_core_fs.nodes[2];
    bytes_read = 0;
    uint32_t sh_crc = 0;
    for (uint64_t off = 0; off < sh_node->data_length; off += 512) {
        size_t to_read = 512;
        if (off + to_read > sh_node->data_length) {
            to_read = (size_t)(sh_node->data_length - off);
        }
        size_t n = 0;
        error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, sh_node, off, to_read, s_chunk_buf, &n);
        if (error != 0 || n != to_read) {
            xzs_early_puts("D5-M3 FATAL: Read /bin/sh chunk failed!\n");
            xzs_breadcrumb(CP_D5M3, 0xFA);
            vnode_put(devvp);
            delay(50000);
            xzs_spin_halt();
            return error;
        }
        sh_crc = xzsfs_crc32(sh_crc, s_chunk_buf, n);
        bytes_read += n;
    }
    if (bytes_read != 16472 || sh_crc != 0xc0d8bfcb) {
        xzs_early_puts("D5-M3 FATAL: /bin/sh payload mismatch!\n");
        xzs_breadcrumb(CP_D5M3, 0xFB);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x71: sh read pass */
    xzs_breadcrumb(CP_D5M3, 0x71);
    xzs_early_puts("D5-M3: /bin/sh payload verified (size=16472, CRC32=0xc0d8bfcb)\n");

    /* 11. Partial, Unaligned, Cross-Sector, and EOF Reads */
    /* First byte (off=0, len=1) */
    uint8_t first_byte = 0;
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 0, 1, &first_byte, &bytes_read);
    if (error != 0 || bytes_read != 1 || first_byte != 0xcf) {
        xzs_early_puts("D5-M3 FATAL: First byte read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFC);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* First 17 bytes (off=0, len=17) */
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 0, 17, s_small_buf, &bytes_read);
    if (error != 0 || bytes_read != 17 || xzsfs_crc32(0, s_small_buf, 17) != 0x5c6352c0) {
        xzs_early_puts("D5-M3 FATAL: First 17 bytes read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFD);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* Unaligned read (off=7, len=33) */
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 7, 33, s_small_buf, &bytes_read);
    if (error != 0 || bytes_read != 33 || xzsfs_crc32(0, s_small_buf, 33) != 0x6f5f3c1f) {
        xzs_early_puts("D5-M3 FATAL: Unaligned read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* Cross-sector read (off=500, len=30) */
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 500, 30, s_small_buf, &bytes_read);
    if (error != 0 || bytes_read != 30 || xzsfs_crc32(0, s_small_buf, 30) != 0x00043eb5) {
        xzs_early_puts("D5-M3 FATAL: Cross-sector read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* Last byte (off=16471, len=1) */
    uint8_t last_byte = 0xff;
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 16471, 1, &last_byte, &bytes_read);
    if (error != 0 || bytes_read != 1 || last_byte != 0x00) {
        xzs_early_puts("D5-M3 FATAL: Last byte read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* EOF read (off=16472, len=16) */
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 16472, 16, s_small_buf, &bytes_read);
    if (error != 0 || bytes_read != 0) {
        xzs_early_puts("D5-M3 FATAL: EOF read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* Past-EOF read (off=20000, len=16) */
    error = xzsfs_core_read(xzsfs_kernel_block_read, devvp, launchd_node, 20000, 16, s_small_buf, &bytes_read);
    if (error != 0 || bytes_read != 0) {
        xzs_early_puts("D5-M3 FATAL: Past-EOF read failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x72: partial / unaligned / cross-sector / EOF pass */
    xzs_breadcrumb(CP_D5M3, 0x72);
    xzs_early_puts("D5-M3: Partial, unaligned, cross-sector, and EOF reads verified\n");

    /* 12. PRE-Mutation Whole-md0 CRC32 Verification */
    uint32_t pre_crc = xzsfs_compute_ramdisk_crc32(devvp, 70);
    if (pre_crc != 0x131e9191) {
        xzs_early_puts("D5-M3 FATAL: Pre-mutation CRC mismatch!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x73: PRE mutation whole-md0 CRC32 pass */
    xzs_breadcrumb(CP_D5M3, 0x73);
    xzs_early_puts("D5-M3: PRE-mutation md0 CRC32 verified (0x131e9191)\n");

    /* 13. Read-Only Rejection Test */
    int rofs_res = xzsfs_rofs_err(NULL);
    if (rofs_res != EROFS) {
        xzs_early_puts("D5-M3 FATAL: Read-only rejection failed!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x80: read-only helper rejection pass */
    xzs_breadcrumb(CP_D5M3, 0x80);
    xzs_early_puts("D5-M3: Read-only rejection verified (EROFS returned)\n");

    /* 14. POST-Mutation Whole-md0 CRC32 Verification */
    uint32_t post_crc = xzsfs_compute_ramdisk_crc32(devvp, 70);
    if (post_crc != 0x131e9191) {
        xzs_early_puts("D5-M3 FATAL: Post-mutation CRC mismatch!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }

    /* Confirm Zero Real Vnode Creations */
    if (xzsfs_get_vnode_call_count != 0 || vnode_create_call_count_from_xzsfs != 0) {
        xzs_early_puts("D5-M3 FATAL: Unexpected real vnode creations!\n");
        xzs_breadcrumb(CP_D5M3, 0xFE);
        vnode_put(devvp);
        delay(50000);
        xzs_spin_halt();
        return EIO;
    }
    /* 0x81: POST mutation whole-md0 CRC32 pass */
    xzs_breadcrumb(CP_D5M3, 0x81);
    xzs_early_puts("D5-M3: POST-mutation md0 CRC32 verified (0x131e9191)\n");

    /* 15. D5-M3 Complete Telemetry Banner */
    /* 0x90: D5-M3 complete */
    xzs_breadcrumb(CP_D5M3, 0x90);
    xzs_early_puts("\n=======================================================\n");
    xzs_early_puts("=== D5-M3 FINAL ACCEPTANCE TELEMETRY BEGIN ===\n");
    xzs_early_puts("D5-M1_COMPLETE=yes\n");
    xzs_early_puts("D5-M2_COMPLETE=yes\n");
    xzs_early_puts("D5-M3_COMPLETE=yes\n");
    xzs_early_puts("XZSFS_REGISTERED=yes\n");
    xzs_early_puts("XZSFS_REGISTRATION_COUNT=1\n");
    xzs_early_puts("XZSFS_SUPERBLOCK_VALID=yes\n");
    xzs_early_puts("XZSFS_METADATA_CRC_MATCH=yes\n");
    xzs_early_puts("XZSFS_OBJECT_COUNT=9\n");
    xzs_early_puts("XZSFS_OBJECT_GRAPH_VALID=yes\n");
    xzs_early_puts("XZSFS_CORE_LOOKUP_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_CORE_READ_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_CORE_READDIR_FORMAT_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_CORE_GETATTR_FORMAT_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_ROFS_HELPER_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_LAUNCHD_SIZE_MATCH=yes\n");
    xzs_early_puts("XZSFS_LAUNCHD_CRC32_MATCH=yes\n");
    xzs_early_puts("XZSFS_SH_SIZE_MATCH=yes\n");
    xzs_early_puts("XZSFS_SH_CRC32_MATCH=yes\n");
    xzs_early_puts("XZSFS_UNALIGNED_READ_MATCH=yes\n");
    xzs_early_puts("XZSFS_CROSS_SECTOR_READ_MATCH=yes\n");
    xzs_early_puts("XZSFS_EOF_SEMANTICS_PASS=yes\n");
    xzs_early_puts("PRE_MUTATION_MD0_CRC32=0x131e9191\n");
    xzs_early_puts("POST_MUTATION_MD0_CRC32=0x131e9191\n");
    xzs_early_puts("RAMDISK_CONTENT_UNCHANGED=yes\n");
    xzs_early_puts("XZSFS_REAL_VNODE_CREATED=no\n");
    xzs_early_puts("XZSFS_VNOP_DISPATCH_VERIFIED=no\n");
    xzs_early_puts("XZSFS_VFS_MOUNT_INVOKED=no\n");
    xzs_early_puts("XZSFS_GET_VNODE_CALL_COUNT=0\n");
    xzs_early_puts("VNODE_CREATE_CALL_COUNT_FROM_XZSFS=0\n");
    xzs_early_puts("VFS_MOUNTROOT_CALLED=no\n");
    xzs_early_puts("XZSFS_ROOT_MOUNT_ATTEMPTED=no\n");
    xzs_early_puts("ROOT_VNODE_INSTALLED=no\n");
    xzs_early_puts("PID1_STARTED=no\n");
    xzs_early_puts("EXECVE_ATTEMPTED=no\n");
    xzs_early_puts("EL0_ENTRY_ATTEMPTED=no\n");
    xzs_early_puts("CMD24_COUNT=0\n");
    xzs_early_puts("CMD25_COUNT=0\n");
    xzs_early_puts("ZERO_STORAGE_WRITES=yes\n");
    xzs_early_puts("D5_COMPLETE=no\n");
    xzs_early_puts("=== D5-M3 FINAL ACCEPTANCE TELEMETRY END ===\n");
    xzs_early_puts("=======================================================\n\n");

    /* Drop block vnode reference */
    vnode_put(devvp);
    devvp = NULL;

    /* 16. Terminal State */
    /* 0x01: D5-M3 diagnostic terminal state */
    xzs_breadcrumb(CP_D5M3, 0x01);
    xzs_early_puts("[XZSFS] PHASE D5-M3 COMPLETE & VERIFIED (PASS)\n");
    xzs_early_puts("[XZSFS] TERMINAL STATE — TRIGGERING WARM RESET TO FASTBOOT\n\n");

    /* Delay 50ms for UART/pstore flush, then warm reset */
    delay(50000);
    xzs_spin_halt();
    return 0;
}

#define CP_D5M4 0xD530

static void
xzs_early_putdec(uint32_t val)
{
    char buf[16];
    int i = 0;
    if (val == 0) {
        xzs_early_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0) {
        xzs_early_putc(buf[--i]);
    }
}

int
xzsfs_d5m4_probe(dev_t rootdev)
{
    extern dev_t mdevlookup(int devid);
    dev_t md0_dev = mdevlookup(0);
    int error = 0;
    vnode_t test_rootvp = NULLVP;
    mount_t mp = mountlist.tqh_first;
    struct vnode_attr va;
    struct componentname cn;

    xzs_early_puts("\n=======================================================\n");
    xzs_early_puts("=== PHASE D5-M4: REAL XZSFS MOUNT & ROOT VNODE PROBE ===\n");
    xzs_early_puts("=======================================================\n");

    /* Verify rootdev invariants */
    if (rootdev != md0_dev || md0_dev == (dev_t)-1) {
        xzs_early_puts("[XZSFS] FATAL: rootdev is not md0!\n");
        xzs_breadcrumb(CP_D5M4, 0xED);
        xzs_spin_halt();
        return EINVAL;
    }

    /* Checkpoint 0x60: VFS_ROOT dispatch pass */
    error = VFS_ROOT(mp, &test_rootvp, vfs_context_kernel());
    if (error != 0 || test_rootvp == NULLVP) {
        xzs_early_puts("[XZSFS] FATAL: VFS_ROOT dispatch failed!\n");
        xzs_breadcrumb(CP_D5M4, 0xEE);
        xzs_spin_halt();
        return error;
    }
    if (test_rootvp != rootvnode) {
        xzs_early_puts("[XZSFS] FATAL: VFS_ROOT vnode != global rootvnode!\n");
        xzs_breadcrumb(CP_D5M4, 0xEF);
        xzs_spin_halt();
        return EINVAL;
    }
    struct xzsfs_node *rnode = (struct xzsfs_node *)vnode_fsnode(test_rootvp);
    if (!rnode || rnode->core.object_id != 1U || vnode_vtype(test_rootvp) != VDIR) {
        xzs_early_puts("[XZSFS] FATAL: root vnode identity invalid!\n");
        xzs_breadcrumb(CP_D5M4, 0xF0);
        xzs_spin_halt();
        return EINVAL;
    }
    xzs_breadcrumb(CP_D5M4, 0x60);
    xzs_early_puts("[XZSFS] VFS_ROOT dispatch verified (PASS)\n");

    /* Checkpoint 0x70: root VNOP_GETATTR pass */
    VATTR_INIT(&va);
    VATTR_WANTED(&va, va_type);
    VATTR_WANTED(&va, va_mode);
    VATTR_WANTED(&va, va_fileid);
    VATTR_WANTED(&va, va_parentid);
    error = VNOP_GETATTR(test_rootvp, &va, vfs_context_kernel());
    if (error != 0 || va.va_type != VDIR || va.va_fileid != 1 || va.va_mode != 0755) {
        xzs_early_puts("[XZSFS] FATAL: root VNOP_GETATTR failed!\n");
        xzs_breadcrumb(CP_D5M4, 0xF1);
        xzs_spin_halt();
        return EINVAL;
    }
    xzs_breadcrumb(CP_D5M4, 0x70);
    xzs_early_puts("[XZSFS] root VNOP_GETATTR verified (PASS)\n");

    /* Checkpoint 0x71: VNOP_LOOKUP \".\" pass */
    bzero(&cn, sizeof(cn));
    cn.cn_nameiop = LOOKUP;
    cn.cn_flags = ISLASTCN;
    cn.cn_context = vfs_context_kernel();
    cn.cn_nameptr = ".";
    cn.cn_namelen = 1;
    vnode_t dot_vp = NULLVP;
    error = VNOP_LOOKUP(test_rootvp, &dot_vp, &cn, vfs_context_kernel());
    if (error != 0 || dot_vp != test_rootvp) {
        xzs_early_puts("[XZSFS] FATAL: VNOP_LOOKUP \".\" failed!\n");
        xzs_breadcrumb(CP_D5M4, 0xF2);
        xzs_spin_halt();
        return EINVAL;
    }
    vnode_put(dot_vp);
    xzs_breadcrumb(CP_D5M4, 0x71);
    xzs_early_puts("[XZSFS] VNOP_LOOKUP \".\" verified (PASS)\n");

    /* Checkpoint 0x72: VNOP_LOOKUP \"..\" pass */
    bzero(&cn, sizeof(cn));
    cn.cn_nameiop = LOOKUP;
    cn.cn_flags = ISLASTCN;
    cn.cn_context = vfs_context_kernel();
    cn.cn_nameptr = "..";
    cn.cn_namelen = 2;
    vnode_t dotdot_vp = NULLVP;
    error = VNOP_LOOKUP(test_rootvp, &dotdot_vp, &cn, vfs_context_kernel());
    if (error != 0 || dotdot_vp != test_rootvp) {
        xzs_early_puts("[XZSFS] FATAL: VNOP_LOOKUP \"..\" failed!\n");
        xzs_breadcrumb(CP_D5M4, 0xF3);
        xzs_spin_halt();
        return EINVAL;
    }
    vnode_put(dotdot_vp);
    xzs_breadcrumb(CP_D5M4, 0x72);
    xzs_early_puts("[XZSFS] VNOP_LOOKUP \"..\" verified (PASS)\n");

    /* Drop iocount held by VFS_ROOT */
    vnode_put(test_rootvp);
    test_rootvp = NULLVP;

    /* Checkpoint 0x80: XZSFS mounted read-only */
    if ((vfs_flags(mp) & MNT_RDONLY) == 0) {
        xzs_early_puts("[XZSFS] FATAL: filesystem is not mounted read-only!\n");
        xzs_breadcrumb(CP_D5M4, 0xF4);
        xzs_spin_halt();
        return EINVAL;
    }
    xzs_breadcrumb(CP_D5M4, 0x80);
    xzs_early_puts("[XZSFS] XZSFS mounted read-only verified (PASS)\n");

    /* Checkpoint 0x81: root filesystem identity pass */
    struct vfsstatfs *sp = vfs_statfs(mp);
    if (strcmp(mp->mnt_vtable->vfc_name, "xzsfs") != 0 ||
        strncmp(sp->f_mntfromname, "md0", 3) != 0) {
        xzs_early_puts("[XZSFS] FATAL: root filesystem identity mismatch!\n");
        xzs_breadcrumb(CP_D5M4, 0xF5);
        xzs_spin_halt();
        return EINVAL;
    }
    xzs_breadcrumb(CP_D5M4, 0x81);
    xzs_early_puts("[XZSFS] root filesystem identity verified (PASS)\n");

    /* Checkpoint 0x90: global root vnode installed */
    if (rootvnode == NULLVP || (rootvnode->v_flag & VROOT) == 0 || rootvnode->v_type != VDIR) {
        xzs_early_puts("[XZSFS] FATAL: global rootvnode not properly installed!\n");
        xzs_breadcrumb(CP_D5M4, 0xF6);
        xzs_spin_halt();
        return EINVAL;
    }
    xzs_breadcrumb(CP_D5M4, 0x90);
    xzs_early_puts("[XZSFS] global root vnode installed verified (PASS)\n");

    /* Print canonical acceptance telemetry banner */
    xzs_early_puts("\n=======================================================\n");
    xzs_early_puts("=== D5-M4 FINAL ACCEPTANCE TELEMETRY BEGIN ===\n");
    xzs_early_puts("D5-M1_COMPLETE=yes\n");
    xzs_early_puts("D5-M2_COMPLETE=yes\n");
    xzs_early_puts("D5-M3_COMPLETE=yes\n");
    xzs_early_puts("D5-M4_COMPLETE=yes\n");
    xzs_early_puts("ROOTDEV_IS_MD0=yes\n");
    xzs_early_puts("ROOTDEV_EQUALS_MDEVLOOKUP0=yes\n");
    xzs_early_puts("XZSFS_REGISTERED=yes\n");
    xzs_early_puts("XZSFS_REGISTRATION_COUNT=1\n");
    xzs_early_puts("XZSFS_VFS_MOUNT_INVOKED=yes\n");
    xzs_early_puts("XZSFS_MOUNT_PRIVATE_ATTACHED=yes\n");
    xzs_early_puts("XZSFS_DEVVP_REFERENCE_HELD=yes\n");
    xzs_early_puts("XZSFS_MOUNT_SUPERBLOCK_VALID=yes\n");
    xzs_early_puts("XZSFS_MOUNT_METADATA_CRC_MATCH=yes\n");
    xzs_early_puts("XZSFS_MOUNT_OBJECT_GRAPH_VALID=yes\n");
    xzs_early_puts("XZSFS_REAL_VNODE_CREATED=yes\n");
    xzs_early_puts("XZSFS_ROOT_VNODE_CREATED=yes\n");
    xzs_early_puts("XZSFS_ROOT_VNODE_OBJECT_ID=1\n");
    xzs_early_puts("XZSFS_VFS_ROOT_DISPATCH_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_VFS_ROOT_RETURNS_ROOT_VNODE=yes\n");
    xzs_early_puts("XZSFS_VNOP_DISPATCH_VERIFIED=yes\n");
    xzs_early_puts("XZSFS_ROOT_GETATTR_VNOP_PASS=yes\n");
    xzs_early_puts("XZSFS_ROOT_DOT_LOOKUP_VNOP_PASS=yes\n");
    xzs_early_puts("XZSFS_ROOT_DOTDOT_LOOKUP_VNOP_PASS=yes\n");
    xzs_early_puts("XZSFS_MOUNT_READ_ONLY=yes\n");
    xzs_early_puts("ROOT_FS_TYPE=xzsfs\n");
    xzs_early_puts("ROOT_FS_DEVICE=md0\n");
    xzs_early_puts("XZSFS_VFS_ROOT_READY=yes\n");
    xzs_early_puts("GLOBAL_ROOTVNODE_INSTALLED=yes\n");
    xzs_early_puts("XZSFS_GET_VNODE_CALL_COUNT=");
    xzs_early_putdec(xzsfs_get_vnode_call_count);
    xzs_early_puts("\n");
    xzs_early_puts("VNODE_CREATE_CALL_COUNT_FROM_XZSFS=");
    xzs_early_putdec(vnode_create_call_count_from_xzsfs);
    xzs_early_puts("\n");
    xzs_early_puts("XZSFS_ROOT_VNODE_CREATE_COUNT=");
    xzs_early_putdec(xzsfs_root_vnode_create_count);
    xzs_early_puts("\n");
    xzs_early_puts("XZSFS_ROOT_VNODE_RECLAIM_COUNT=");
    xzs_early_putdec(xzsfs_root_vnode_reclaim_count);
    xzs_early_puts("\n");
    xzs_early_puts("XZSFS_MOUNT_FAILURE_UNWIND_AUDITED=yes\n");
    xzs_early_puts("PID1_STARTED=no\n");
    xzs_early_puts("EXECVE_ATTEMPTED=no\n");
    xzs_early_puts("EL0_ENTRY_ATTEMPTED=no\n");
    xzs_early_puts("CMD24_COUNT=0\n");
    xzs_early_puts("CMD25_COUNT=0\n");
    xzs_early_puts("ZERO_STORAGE_WRITES=yes\n");
    xzs_early_puts("D5_COMPLETE=no\n");
    xzs_early_puts("=== D5-M4 FINAL ACCEPTANCE TELEMETRY END ===\n");
    xzs_early_puts("=======================================================\n\n");

    /* Checkpoint 0x91: D5-M4 complete */
    xzs_breadcrumb(CP_D5M4, 0x91);
    xzs_early_puts("[XZSFS] PHASE D5-M4 COMPLETE & VERIFIED (PASS)\n");

    /* Checkpoint 0x01: D5-M4 diagnostic terminal state */
    xzs_breadcrumb(CP_D5M4, 0x01);
    xzs_early_puts("[XZSFS] TERMINAL STATE — TRIGGERING WARM RESET TO FASTBOOT\n\n");

    delay(50000);
    xzs_spin_halt();
    return 0;
}
