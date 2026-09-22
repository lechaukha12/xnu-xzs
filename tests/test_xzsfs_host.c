/*
 * XZSFS Host Unit Test Suite
 *
 * Tests xzsfs_core against the frozen D5-M1 image artifacts/builds/xzs-rootfs.img.
 * Exercises both positive image verification and the full negative corruption matrix.
 */

#include "xzsfs_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct host_block_ctx {
    FILE *fp;
};

static int host_block_read(void *ctx, uint64_t lba, uint32_t count, void *buf) {
    struct host_block_ctx *h = (struct host_block_ctx *)ctx;
    if (fseek(h->fp, (long)(lba * XZSFS_SECTOR_SIZE), SEEK_SET) != 0) {
        return XZSFS_ERR_IO;
    }
    size_t n = fread(buf, XZSFS_SECTOR_SIZE, count, h->fp);
    if (n != count) {
        return XZSFS_ERR_IO;
    }
    return XZSFS_ERR_OK;
}

int main(int argc, char **argv) {
    const char *img_path = (argc > 1) ? argv[1] : "artifacts/builds/xzs-rootfs.img";
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open image %s\n", img_path);
        return 1;
    }

    struct host_block_ctx bctx = { .fp = fp };
    struct xzsfs_disk_superblock sb;
    int err = xzsfs_core_read_superblock(host_block_read, &bctx, &sb);
    assert(err == XZSFS_ERR_OK);
    printf("[PASS] Superblock read: magic=0x%08x, version=%u, objects=%u\n", sb.magic, sb.format_version, sb.object_count);

    err = xzsfs_core_validate_superblock(&sb);
    assert(err == XZSFS_ERR_OK);
    printf("[PASS] Superblock validate\n");

    struct xzsfs_disk_object objects[XZSFS_MAX_OBJECTS];
    char strtab[XZSFS_MAX_STRTAB_SIZE];
    err = xzsfs_core_load_metadata(host_block_read, &bctx, &sb, objects, XZSFS_MAX_OBJECTS, strtab, XZSFS_MAX_STRTAB_SIZE);
    assert(err == XZSFS_ERR_OK);
    printf("[PASS] Metadata loaded\n");

    err = xzsfs_core_validate_metadata(&sb, objects, strtab);
    assert(err == XZSFS_ERR_OK);
    printf("[PASS] Metadata validate: CRC32=0x%08x matched!\n", sb.metadata_crc32);

    struct xzsfs_core_fs fs;
    err = xzsfs_core_init_fs(&fs, &sb, objects, strtab);
    assert(err == XZSFS_ERR_OK);

    /* Test Path Lookups */
    const struct xzsfs_core_node *node = NULL;

    /* /dev */
    err = xzsfs_core_lookup(&fs, 1, "dev", 3, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 4 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /dev (id=4, dir)\n");

    /* /sbin */
    err = xzsfs_core_lookup(&fs, 1, "sbin", 4, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 9 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /sbin (id=9, dir)\n");

    /* /sbin/launchd */
    err = xzsfs_core_lookup(&fs, 9, "launchd", 7, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 10 && node->type == XZSFS_TYPE_REG);
    printf("[PASS] Lookup /sbin/launchd (id=10, reg)\n");

    /* /bin */
    err = xzsfs_core_lookup(&fs, 1, "bin", 3, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 2 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /bin (id=2, dir)\n");

    /* /bin/sh */
    err = xzsfs_core_lookup(&fs, 2, "sh", 2, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 5 && node->type == XZSFS_TYPE_REG);
    printf("[PASS] Lookup /bin/sh (id=5, reg)\n");

    /* /etc */
    err = xzsfs_core_lookup(&fs, 1, "etc", 3, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 7 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /etc (id=7, dir)\n");

    /* /tmp */
    err = xzsfs_core_lookup(&fs, 1, "tmp", 3, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 11 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /tmp (id=11, dir)\n");

    /* /var */
    err = xzsfs_core_lookup(&fs, 1, "var", 3, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 12 && node->type == XZSFS_TYPE_DIR);
    printf("[PASS] Lookup /var (id=12, dir)\n");

    /* Dot and dotdot lookup */
    err = xzsfs_core_lookup(&fs, 9, ".", 1, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 9);
    printf("[PASS] Lookup /sbin/. -> /sbin\n");

    err = xzsfs_core_lookup(&fs, 9, "..", 2, &node);
    assert(err == XZSFS_ERR_OK && node->object_id == 1);
    printf("[PASS] Lookup /sbin/.. -> /\n");

    /* Missing name lookup -> ENOENT */
    err = xzsfs_core_lookup(&fs, 1, "nonexistent", 11, &node);
    assert(err == XZSFS_ERR_NOENT);
    printf("[PASS] Lookup nonexistent child -> ENOENT\n");

    /* Lookup inside regular file -> ENOTDIR */
    err = xzsfs_core_lookup(&fs, 10, "child", 5, &node);
    assert(err == XZSFS_ERR_NOTDIR);
    printf("[PASS] Lookup inside regular file -> ENOTDIR\n");

    /* Payload Verification: /sbin/launchd */
    uint8_t *launchd_buf = (uint8_t *)malloc(16472);
    size_t bytes_read = 0;
    const struct xzsfs_core_node *launchd_node = NULL;
    xzsfs_core_lookup(&fs, 9, "launchd", 7, &launchd_node);
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 0, launchd_node->data_length, launchd_buf, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 16472);
    uint32_t launchd_crc = xzsfs_crc32(0, launchd_buf, bytes_read);
    printf("[PASS] /sbin/launchd: size=%zu, CRC32=0x%08x\n", bytes_read, launchd_crc);
    assert(launchd_crc == 0xe212a8a2);

    /* Payload Verification: /bin/sh */
    uint8_t *sh_buf = (uint8_t *)malloc(16736);
    const struct xzsfs_core_node *sh_node = NULL;
    xzsfs_core_lookup(&fs, 2, "sh", 2, &sh_node);
    err = xzsfs_core_read(host_block_read, &bctx, sh_node, 0, sh_node->data_length, sh_buf, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 16736);
    uint32_t sh_crc = xzsfs_crc32(0, sh_buf, bytes_read);
    printf("[PASS] /bin/sh: size=%zu, CRC32=0x%08x\n", bytes_read, sh_crc);
    assert(sh_crc == 0x79551619);

    /* Partial & Unaligned Read Tests */
    /* 1. First byte */
    uint8_t first_byte = 0;
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 0, 1, &first_byte, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 1 && first_byte == 0xcf);
    printf("[PASS] /sbin/launchd first byte: 0x%02x\n", first_byte);

    /* 2. First 17 bytes */
    uint8_t b17[17];
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 0, 17, b17, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 17);
    uint32_t crc17 = xzsfs_crc32(0, b17, 17);
    printf("[PASS] /sbin/launchd first 17 bytes: CRC32=0x%08x\n", crc17);
    assert(crc17 == 0x5c6352c0);

    /* 3. Unaligned offset 7, length 33 */
    uint8_t unaligned_buf[33];
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 7, 33, unaligned_buf, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 33);
    uint32_t unaligned_crc = xzsfs_crc32(0, unaligned_buf, 33);
    printf("[PASS] unaligned read (off=7, len=33): CRC32=0x%08x\n", unaligned_crc);
    assert(unaligned_crc == 0x6f5f3c1f);

    /* 4. Cross sector read offset 500, length 30 (spans 500..530 across sector boundary 512) */
    uint8_t cross_buf[30];
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 500, 30, cross_buf, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 30);
    uint32_t cross_crc = xzsfs_crc32(0, cross_buf, 30);
    printf("[PASS] cross sector read (off=500, len=30): CRC32=0x%08x\n", cross_crc);
    assert(cross_crc == 0x00043eb5);

    /* 5. Last byte (offset 16471) */
    uint8_t last_byte = 0xff;
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 16471, 1, &last_byte, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 1 && last_byte == 0x00);
    printf("[PASS] /sbin/launchd last byte (off=16471): 0x%02x\n", last_byte);

    /* 6. EOF read (offset 16472) */
    uint8_t eof_buf[16];
    err = xzsfs_core_read(host_block_read, &bctx, launchd_node, 16472, 16, eof_buf, &bytes_read);
    assert(err == XZSFS_ERR_OK && bytes_read == 0);
    printf("[PASS] EOF read (off=16472, len=16): 0 bytes returned\n");

    /* ========================================================================= */
    /* FULL NEGATIVE TEST MATRIX (13 DISTINCT CORRUPTIONS)                       */
    /* ========================================================================= */
    printf("\n--- EXECUTING FULL NEGATIVE TEST MATRIX (13 TESTS) ---\n");
    int neg_test_count = 0;

    /* Neg 1: Bad magic */
    {
        struct xzsfs_disk_superblock corrupt_sb = sb;
        corrupt_sb.magic = 0xdeadbeef;
        assert(xzsfs_core_validate_superblock(&corrupt_sb) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 1: Bad magic rejected\n");
    }

    /* Neg 2: Bad version */
    {
        struct xzsfs_disk_superblock corrupt_sb = sb;
        corrupt_sb.format_version = 99;
        assert(xzsfs_core_validate_superblock(&corrupt_sb) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 2: Bad version rejected\n");
    }

    /* Neg 3: Bad metadata CRC */
    {
        struct xzsfs_disk_superblock corrupt_sb = sb;
        corrupt_sb.metadata_crc32 ^= 0x12345678;
        assert(xzsfs_core_validate_metadata(&corrupt_sb, objects, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 3: Bad metadata CRC rejected\n");
    }

    /* Neg 4: Bad object count / table bounds */
    {
        struct xzsfs_disk_superblock corrupt_sb = sb;
        corrupt_sb.object_count = 0;
        assert(xzsfs_core_validate_superblock(&corrupt_sb) != XZSFS_ERR_OK);

        corrupt_sb = sb;
        corrupt_sb.object_table_size = 128; /* Not matching count * 64 */
        assert(xzsfs_core_validate_superblock(&corrupt_sb) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 4: Bad object count/table bounds rejected\n");
    }

    /* Neg 5: Duplicate object ID */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[2].object_id = corrupt_obj[1].object_id; /* Duplicate ID 2 */
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 5: Duplicate object ID rejected\n");
    }

    /* Neg 6: Invalid parent ID (parent == 0) */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[2].parent_id = 0;
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 6: Invalid parent ID (0) rejected\n");
    }

    /* Neg 7: Parent cycle (parent >= child ID) */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[2].parent_id = 3; /* points to itself (child ID is 3) */
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);

        corrupt_obj[2].parent_id = 5; /* forward reference / cycle */
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 7: Parent cycle rejected\n");
    }

    /* Neg 8: String offset out of bounds */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[2].name_offset = (uint32_t)sb.string_table_size + 10;
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 8: String offset out of bounds rejected\n");
    }

    /* Neg 9: Missing NUL termination in string table */
    {
        char corrupt_strtab[XZSFS_MAX_STRTAB_SIZE];
        memcpy(corrupt_strtab, strtab, sizeof(strtab));
        /* Replace NUL terminator for /bin/sh ("sh\0") with 'X' */
        uint32_t sh_name_off = objects[2].name_offset;
        uint32_t sh_name_len = objects[2].name_length;
        corrupt_strtab[sh_name_off + sh_name_len] = 'X';
        assert(xzsfs_core_validate_metadata(&sb, objects, corrupt_strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 9: Missing NUL termination rejected\n");
    }

    /* Neg 10: Payload out of bounds */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[6].data_offset = sb.image_size_bytes; /* At or beyond EOF */
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);

        memcpy(corrupt_obj, objects, sizeof(objects));
        corrupt_obj[6].data_length = sb.image_size_bytes; /* Extends past EOF */
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 10: Payload out of bounds rejected\n");
    }

    /* Neg 11: Payload overlap */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        /* Overlap /sbin/launchd (obj 6) with /bin/sh (obj 2): set obj 6 data_offset into obj 2's extent */
        corrupt_obj[6].data_offset = corrupt_obj[2].data_offset;
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 11: Overlapping file payloads rejected\n");
    }

    /* Neg 12: Directory carrying invalid payload */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        /* Give /bin (obj 1, dir) a non-zero data length or offset */
        corrupt_obj[1].data_length = 512;
        corrupt_obj[1].data_offset = 2048;
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 12: Directory carrying invalid payload rejected\n");
    }

    /* Neg 13: Duplicate child name within the same directory */
    {
        struct xzsfs_disk_object corrupt_obj[XZSFS_MAX_OBJECTS];
        memcpy(corrupt_obj, objects, sizeof(objects));
        /* Make object 4 (/dev) have the same parent (1) and same name offset as object 1 (/bin) */
        corrupt_obj[3].name_offset = corrupt_obj[1].name_offset;
        corrupt_obj[3].name_length = corrupt_obj[1].name_length;
        assert(xzsfs_core_validate_metadata(&sb, corrupt_obj, strtab) != XZSFS_ERR_OK);
        neg_test_count++;
        printf("[PASS] Negative 13: Duplicate child name in directory rejected\n");
    }

    printf("\n=======================================================\n");
    printf("XZSFS_KERNEL_PARSER_NEGATIVE_TEST_COUNT=%d\n", neg_test_count);
    printf("XZSFS_KERNEL_PARSER_ALL_NEGATIVE_TESTS_REJECTED=yes\n");
    printf("KERNEL_PARSER_HOST_POSITIVE_TESTS_PASS=yes\n");
    printf("KERNEL_PARSER_HOST_NEGATIVE_TESTS_PASS=yes\n");
    printf("=======================================================\n");

    free(launchd_buf);
    free(sh_buf);
    fclose(fp);
    return 0;
}
