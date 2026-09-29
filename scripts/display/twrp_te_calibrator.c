/*
 * TWRP TE Calibrator: High-frequency passive polling of GPIO10 pad on Sony Xperia XZs.
 * Freestanding ARM64 binary with zero libc dependencies.
 */

typedef unsigned long u64;
typedef unsigned int u32;

static inline long sys_read(int fd, void *buf, u64 count) {
    register long x8 __asm__("x8") = 63;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long sys_write(int fd, const void *buf, u64 count) {
    register long x8 __asm__("x8") = 64;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = count;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long sys_openat(int dirfd, const char *pathname, int flags) {
    register long x8 __asm__("x8") = 56;
    register long x0 __asm__("x0") = dirfd;
    register long x1 __asm__("x1") = (long)pathname;
    register long x2 __asm__("x2") = flags;
    register long x3 __asm__("x3") = 0;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline long sys_close(int fd) {
    register long x8 __asm__("x8") = 57;
    register long x0 __asm__("x0") = fd;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long sys_lseek(int fd, long offset, int whence) {
    register long x8 __asm__("x8") = 62;
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = offset;
    register long x2 __asm__("x2") = whence;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline void sys_exit(int code) {
    register long x8 __asm__("x8") = 93;
    register long x0 __asm__("x0") = code;
    __asm__ volatile("svc #0" : : "r"(x8), "r"(x0) : "memory");
    __builtin_unreachable();
}

struct timespec {
    long tv_sec;
    long tv_nsec;
};

static inline long sys_clock_gettime(int clk_id, struct timespec *tp) {
    register long x8 __asm__("x8") = 113;
    register long x0 __asm__("x0") = clk_id;
    register long x1 __asm__("x1") = (long)tp;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1) : "memory");
    return x0;
}

static void print_str(const char *s) {
    int len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

static void print_u64(u64 val) {
    char buf[32];
    int i = 0;
    if (val == 0) {
        buf[i++] = '0';
    } else {
        char temp[32];
        int t = 0;
        while (val > 0) {
            temp[t++] = '0' + (val % 10);
            val /= 10;
        }
        while (t > 0) {
            buf[i++] = temp[--t];
        }
    }
    buf[i] = '\0';
    print_str(buf);
}

void _start(void) {
    const char *path = "/sys/class/gpio/gpio10/value";
    int fd = sys_openat(-100, path, 0); // AT_FDCWD = -100, O_RDONLY = 0
    if (fd < 0) {
        print_str("ERROR: Failed to open ");
        print_str(path);
        print_str("\n");
        sys_exit(1);
    }

    struct timespec ts_start, ts_now;
    sys_clock_gettime(1, &ts_start); // CLOCK_MONOTONIC = 1

    u64 target_duration_ns = 1500000000ULL; // 500 ms window (>30 frames @ 60 Hz)
    u64 start_ns = (u64)ts_start.tv_sec * 1000000000ULL + ts_start.tv_nsec;

    char c = '0';
    sys_read(fd, &c, 1);
    u32 prev_val = (c == '1') ? 1 : 0;

    u64 total_samples = 0;
    u64 low_samples = 0;
    u64 high_samples = 0;
    u64 transitions = 0;

    while (1) {
        sys_lseek(fd, 0, 0); // SEEK_SET = 0
        if (sys_read(fd, &c, 1) > 0) {
            u32 val = (c == '1') ? 1 : 0;
            total_samples++;
            if (val == 1) {
                high_samples++;
            } else {
                low_samples++;
            }
            if (val != prev_val) {
                transitions++;
                prev_val = val;
            }
        }

        sys_clock_gettime(1, &ts_now);
        u64 now_ns = (u64)ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec;
        if (now_ns - start_ns >= target_duration_ns) {
            break;
        }
    }

    sys_close(fd);

    u64 total_elapsed_ns = (u64)ts_now.tv_sec * 1000000000ULL + ts_now.tv_nsec - start_ns;
    u64 total_elapsed_us = total_elapsed_ns / 1000ULL;
    u64 avg_interval_ns = (total_samples > 0) ? (total_elapsed_ns / total_samples) : 0;

    print_str("=== TWRP GPIO10 TE CALIBRATION RESULT ===\n");
    print_str("TOTAL_SAMPLES="); print_u64(total_samples); print_str("\n");
    print_str("LOW_SAMPLES="); print_u64(low_samples); print_str("\n");
    print_str("HIGH_SAMPLES="); print_u64(high_samples); print_str("\n");
    print_str("GPIO10_TRANSITIONS="); print_u64(transitions); print_str("\n");
    print_str("OBSERVATION_ELAPSED_US="); print_u64(total_elapsed_us); print_str("\n");
    print_str("AVG_SAMPLE_INTERVAL_NS="); print_u64(avg_interval_ns); print_str("\n");
    print_str("PHYSICAL_TE_OBSERVED="); print_str(transitions > 0 ? "YES\n" : "NO\n");

    sys_exit(0);
}
