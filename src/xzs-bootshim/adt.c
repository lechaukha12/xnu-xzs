#include "adt.h"

struct dt_node_hdr {
    uint32_t nProperties;
    uint32_t nChildren;
};

struct dt_prop_hdr {
    char name[32];
    uint32_t length;
};

static uint8_t *adt_ptr;

static void mem_set(void *dst, int val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)val;
}

static size_t str_len(const char *s) {
    size_t l = 0;
    while (*s++) l++;
    return l;
}

static void str_copy_pad(char *dst, const char *src, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < maxlen) {
        dst[i++] = '\0';
    }
}

static void adt_put_node(uint32_t nprops, uint32_t nchildren) {
    struct dt_node_hdr *hdr = (struct dt_node_hdr *)adt_ptr;
    hdr->nProperties = nprops;
    hdr->nChildren = nchildren;
    adt_ptr += sizeof(struct dt_node_hdr);
}

static void adt_put_prop(const char *name, const void *val, uint32_t len) {
    struct dt_prop_hdr *hdr = (struct dt_prop_hdr *)adt_ptr;
    str_copy_pad(hdr->name, name, 32);
    hdr->length = len;
    adt_ptr += sizeof(struct dt_prop_hdr);

    uint8_t *d = adt_ptr;
    const uint8_t *s = (const uint8_t *)val;
    for (uint32_t i = 0; i < len; i++) {
        d[i] = s[i];
    }
    uint32_t padded_len = (len + 3) & ~3U;
    for (uint32_t i = len; i < padded_len; i++) {
        d[i] = 0;
    }
    adt_ptr += padded_len;
}

static void adt_put_str(const char *name, const char *str) {
    adt_put_prop(name, str, (uint32_t)(str_len(str) + 1));
}

static void adt_put_u32(const char *name, uint32_t val) {
    adt_put_prop(name, &val, 4);
}

static void adt_put_u64(const char *name, uint64_t val) {
    adt_put_prop(name, &val, 8);
}

static void adt_put_u64_array(const char *name, const uint64_t *vals, uint32_t count) {
    adt_put_prop(name, vals, count * 8);
}

uint32_t adt_build_tree_at(uint64_t base_addr) {
    uint8_t *start = (uint8_t *)base_addr;
    adt_ptr = start;

    /*
     * Node hierarchy:
     * / (device-tree) -> 4 children: chosen, defaults, cpus, arm-io
     */
    adt_put_node(4, 4);
    adt_put_str("name", "device-tree");
    adt_put_str("model", "Sony Xperia XZs (MSM8996)");
    adt_put_str("target-type", "VMApple");
    adt_put_str("compatible", "sony,tone-keyaki");

    /* Child 1: /chosen -> 1 child (memory-map) */
    adt_put_node(6, 1);
    adt_put_str("name", "chosen");
    adt_put_u32("debug-enabled", 1);
    adt_put_u64("dram-base", 0x80000000ULL);
    adt_put_u64("dram-size", 0x05800000ULL);   /* 88 MB safe low DRAM (stops before 0x85800000 TrustZone/HYP) */
    adt_put_str("firmware-version", "xzs-bootshim-1.0");
    static const uint8_t random_seed[64] = {
        0x58, 0x5a, 0x53, 0x2d, 0x58, 0x4e, 0x55, 0x2d, /* "XZS-XNU-" */
        0x52, 0x41, 0x4e, 0x44, 0x4f, 0x4d, 0x2d, 0x53, /* "RANDOM-S" */
        0x45, 0x45, 0x44, 0x2d, 0x32, 0x30, 0x32, 0x36, /* "EED-2026" */
        0x30, 0x39, 0x31, 0x36, 0x2d, 0x4b, 0x45, 0x59, /* "0916-KEY" */
        0x41, 0x4b, 0x49, 0x2d, 0x4d, 0x53, 0x4d, 0x38, /* "AKI-MSM8" */
        0x39, 0x39, 0x36, 0x2d, 0x47, 0x38, 0x32, 0x33, /* "996-G823" */
        0x31, 0x2d, 0x51, 0x55, 0x41, 0x4c, 0x43, 0x4f, /* "1-QUALCO" */
        0x4d, 0x4d, 0x2d, 0x4b, 0x52, 0x59, 0x4f, 0x21  /* "MM-KRYO!" */
    };
    adt_put_prop("random-seed", random_seed, sizeof(random_seed));

    /* Child 1.1: /chosen/memory-map -> 0 children */
    adt_put_node(2, 0);
    adt_put_str("name", "memory-map");
    static const struct {
        uint64_t paddr;
        uint64_t length;
    } ramdisk_range = {
        .paddr = 0x81700000ULL,
        .length = 0x12000ULL,    /* 73728 bytes, image plus zero padding */
    };
    adt_put_prop("RAMDisk", &ramdisk_range, sizeof(ramdisk_range));

    /* Child 2: /defaults -> 0 children */
    adt_put_node(2, 0);
    adt_put_str("name", "defaults");
    adt_put_u32("serial-device", 0x10);

    /* Child 3: /cpus -> 4 children (cpu0, cpu1, cpu2, cpu3) */
    adt_put_node(1, 4);
    adt_put_str("name", "cpus");

    /* Child 3.1: /cpus/cpu0 -> 0 children (Cluster 0, Core 0, MPIDR 0x0) */
    adt_put_node(7, 0);
    adt_put_str("name", "cpu0");
    adt_put_str("state", "running");
    adt_put_u32("reg", 0);                         /* Mandatory CPU physical ID for ml_parse_cpu_topology */
    adt_put_u32("timebase-frequency", 19200000);   /* 19.2 MHz ARM Generic Timer */
    adt_put_u32("bus-frequency", 100000000);        /* 100 MHz bus */
    adt_put_u32("clock-frequency", 2150000000U);    /* 2.15 GHz Kryo Gold */
    adt_put_u32("memory-frequency", 1866000000U);   /* 1.866 GHz LPDDR4 */

    /* Child 3.2: /cpus/cpu1 -> 0 children (Cluster 0, Core 1, MPIDR 0x1) */
    adt_put_node(7, 0);
    adt_put_str("name", "cpu1");
    adt_put_str("state", "stopped");
    adt_put_u32("reg", 1);                         /* Cluster 0, Core 1 */
    adt_put_u32("timebase-frequency", 19200000);   /* 19.2 MHz ARM Generic Timer */
    adt_put_u32("bus-frequency", 100000000);        /* 100 MHz bus */
    adt_put_u32("clock-frequency", 2150000000U);    /* 2.15 GHz Kryo Gold */
    adt_put_u32("memory-frequency", 1866000000U);   /* 1.866 GHz LPDDR4 */

    /* Child 3.3: /cpus/cpu2 -> 0 children (Cluster 1, Core 0, MPIDR 0x100) */
    adt_put_node(7, 0);
    adt_put_str("name", "cpu2");
    adt_put_str("state", "stopped");
    adt_put_u32("reg", 0x100);                     /* Cluster 1, Core 0 */
    adt_put_u32("timebase-frequency", 19200000);   /* 19.2 MHz ARM Generic Timer */
    adt_put_u32("bus-frequency", 100000000);        /* 100 MHz bus */
    adt_put_u32("clock-frequency", 2150000000U);    /* 2.15 GHz Kryo Gold */
    adt_put_u32("memory-frequency", 1866000000U);   /* 1.866 GHz LPDDR4 */

    /* Child 3.4: /cpus/cpu3 -> 0 children (Cluster 1, Core 1, MPIDR 0x101) */
    adt_put_node(7, 0);
    adt_put_str("name", "cpu3");
    adt_put_str("state", "stopped");
    adt_put_u32("reg", 0x101);                     /* Cluster 1, Core 1 */
    adt_put_u32("timebase-frequency", 19200000);   /* 19.2 MHz ARM Generic Timer */
    adt_put_u32("bus-frequency", 100000000);        /* 100 MHz bus */
    adt_put_u32("clock-frequency", 2150000000U);    /* 2.15 GHz Kryo Gold */
    adt_put_u32("memory-frequency", 1866000000U);   /* 1.866 GHz LPDDR4 */

    /* Child 4: /arm-io -> 3 children (uart, gic, timer) */
    /* ranges: child_bus_base=0, parent_phys_base=0x01000000, size=0x10000000 (256MB)
     * pe_arm_get_soc_base_phys() returns ranges[1] = 0x01000000 (non-zero).
     */
    uint64_t ranges[] = { 0ULL, 0x01000000ULL, 0x10000000ULL };
    adt_put_node(3, 3);
    adt_put_str("name", "arm-io");
    adt_put_str("device_type", "arm-io");
    adt_put_u64_array("ranges", ranges, 3);

    /* Child 4.1: /arm-io/uart -> 0 children */
    /* Physical 0x075b0000 - 0x01000000 = 0x065b0000 */
    uint64_t uart_reg[] = { 0x065b0000ULL, 0x1000ULL };
    adt_put_node(5, 0);
    adt_put_str("name", "uart");
    adt_put_str("device_type", "serial");
    adt_put_str("compatible", "qcom,msm-uartdm");
    adt_put_u32("AAPL,phandle", 0x10);
    adt_put_u64_array("reg", uart_reg, 2);

    /* Child 4.2: /arm-io/gic -> 0 children */
    /* Satisfies both SecureDTLookupEntry("/arm-io/gic") in pe_fiq.c
     * and SecureDTFindEntry("interrupt-controller", "master") in pe_identify_machine.c.
     * MSM8996 GICv3: GICD physical 0x09bc0000 (offset 0x08bc0000, size 0x10000)
     *                GICR physical 0x09c00000 (offset 0x08c00000, size 0x100000)
     */
    uint64_t gic_reg[] = { 0x08bc0000ULL, 0x10000ULL, 0x08c00000ULL, 0x100000ULL };
    adt_put_node(4, 0);
    adt_put_str("name", "gic");
    adt_put_str("device_type", "interrupt-controller");
    adt_put_str("interrupt-controller", "master");
    adt_put_u64_array("reg", gic_reg, 4);

    /* Child 4.3: /arm-io/timer -> 0 children */
    /* Satisfies SecureDTFindEntry("device_type", "timer") in pe_identify_machine.c.
     * MSM8996 Timer physical 0x02c00000 (offset 0x01c00000, size 0x1000)
     */
    uint64_t timer_reg[] = { 0x01c00000ULL, 0x1000ULL };
    adt_put_node(3, 0);
    adt_put_str("name", "timer");
    adt_put_str("device_type", "timer");
    adt_put_u64_array("reg", timer_reg, 2);

    return (uint32_t)(adt_ptr - start);
}

uint32_t adt_build_tree(void) {
    return adt_build_tree_at(ADT_BASE_ADDR);
}
