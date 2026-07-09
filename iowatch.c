#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/fanotify.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/statfs.h>
#include <dirent.h>
#include <ctype.h>

#define VERSION "7.7.0-intelligent"
#define MAX_EVENTS 8192
#define MAX_PID_CACHE 8192
#define MAX_DISKS 128
#define RING_BUFFER_SIZE 32768
#define BLOCK_RING_SIZE 4096
#define SAMPLE_INTERVAL_MS 100
#define IDLE_THRESHOLD 300
#define INODE_CACHE_SIZE 16384
#define MAX_OWNERS_PER_INODE 8
#define CGROUP_PATH_MAX 256
#define TRACEFS_PERCPU "/sys/kernel/tracing/per_cpu/cpu"
#define MAX_RECURSE_DEPTH 16
#define CONTAINER_INFO_LEN 384
#define CMD_BUF_LEN 8192
#define ZFS_PENDING_MAX 2048

/* ==================== 结构体定义==================== */
typedef struct {
    uint64_t events, real_io, cache_hit;
    uint64_t r_bytes, w_bytes, dirty_bytes;
    time_t first_seen, last_seen, last_real_io;
    char container[CGROUP_PATH_MAX];
} stats_t;

typedef struct {
    pid_t pid;
    pid_t ppid;
    char comm[16];
    char parent_comm[16];
    uint64_t r_snap, w_snap;
    time_t cache_t;
    int valid;
    int is_kthread;
    char ktype[32];
} proc_info_t;

typedef struct {
    ino_t inode;
    dev_t dev;
    struct owner {
        pid_t pid;
        char comm[16];
        time_t dirty_time;
        uint64_t dirty_bytes;
        int active;
    } owners[MAX_OWNERS_PER_INODE];
    int count;
} inode_history_t;

typedef struct {
    struct timespec ts;
    pid_t pid;
    char path[PATH_MAX];
    uint64_t mask;
    stats_t *st_ptr;
    char comm_cache[16];
    char parent_comm_cache[16];
    pid_t ppid_cache;
    char container[CGROUP_PATH_MAX];
    int is_atime;
    ino_t inode;
    dev_t dev;
} history_entry_t;

typedef struct {
    struct timespec ts;
    char comm[32], disk[32];
    uint64_t sector, bytes;
    int write;
    int pid;
} block_event_t;

/* ==================== 内部使用结构体==================== */
typedef struct {
    pid_t user_pid;
    char user_comm[16];
    uint64_t last_mask;
    time_t last_ts;
} fs_shadow_t;

typedef struct {
    history_entry_t ev;
    time_t enqueue_ts;
    int is_pending;
} zfs_pending_ev_t;

/* ==================== 全局变量==================== */
int debug_mode = 0;
int quiet_mode = 0;
int daemon_mode = 0;
int monitor_read = 0;
int filter_pid = 0;
int test_mode = 0;
int g_swap_on_disk = 0;
volatile sig_atomic_t exit_flag = 0;
int fan_fd = -1, trace_fd = -1;
time_t start_t, idle_t, scheduled_start = 0, run_duration = 0, roll_interval = 0, last_roll_time = 0;
int g_disk_cnt = 0;
int g_state = 0;
int self_pid = 0;
char disk_names[512] = {0};
char g_swap_dev[64] = {0};
char *filter_process = NULL;
char *log_file = NULL;
char *start_time_str = NULL;
char *duration_str = NULL;
char *roll_interval_str = NULL;
FILE *csv_stream = NULL;
char current_log_file[PATH_MAX] = {0};

struct { char name[32]; uint64_t last_total; } g_disks[MAX_DISKS];

pthread_mutex_t ring_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ring_cnd = PTHREAD_COND_INITIALIZER;
pthread_mutex_t block_mtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t block_cnd = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cache_mtx = PTHREAD_MUTEX_INITIALIZER;

static struct { history_entry_t buffer[RING_BUFFER_SIZE]; volatile int head, tail, count; } g_ring;
static struct { block_event_t buffer[BLOCK_RING_SIZE]; volatile int head, tail, count; } b_ring;

proc_info_t p_cache[MAX_PID_CACHE];
stats_t p_stats[MAX_PID_CACHE];
int p_hash[MAX_PID_CACHE];
int p_count = 0;

inode_history_t inode_hist[INODE_CACHE_SIZE];
int inode_hist_count = 0;

pthread_t prod, cons, trace_th, block_th;

/* ==================== 环境探测变量==================== */
static int g_fs_type = 0;
static int g_has_noatime = 0;
static char g_zfs_pool[64] = {0};
static int g_has_docker = 0;
static int g_is_hdd = 0;
static fs_shadow_t g_shadow = {0};
static zfs_pending_ev_t g_zfs_pending[ZFS_PENDING_MAX]; // ZFS Pending队列
static int g_zfs_pending_cnt = 0;
static uint64_t g_zfs_last_sync_done = 0;
static int g_zfs_txg_enabled = 0;

/* ==================== 语言包==================== */
typedef struct {
    const char *title;
    const char *mon_start;
    const char *disks;
    const char *header_title;
    const char *data_format;
    const char *type_real;
    const char *type_cache;
    const char *type_block;
    const char *warn_swap;
    const char *warn_atime;
    const char *ghost_io;
    const char *report_title;
    const char *suspect;
    const char *host_label;
    const char *container_label;
    const char *confidence;
    const char *standby;
    const char *wakeup;
    const char *attributed;
    const char *unattributed;
    const char *waiting_start;
    const char *duration_expired;
    const char *daemon_mode;
    const char *log_rotated;
    const char *quiet_mode;
    const char *help_text;
    /* 新增诊断字段 */
    const char *diag_header;
    const char *env_diag;
    const char *advice_header;
    const char *advice_atime;
    const char *advice_docker;
    const char *advice_commit;
    const char *advice_swap;
    const char *advice_log;
} lang_t;

static const lang_t lang_en = {
    .title = "IOWATCH v" VERSION " (Intelligent Analyzer)",
    .mon_start = "Monitoring started",
    .disks = "Physical Disks",
    .header_title = "%-23s %-7s %-7s %-17s %-7s %-17s %-8s %s %s\n",
    .data_format = "%-23s %-7s PID:%-8d [%-15s] <- PPID:%-8d [%-15s] %-8s %s %s\n",
    .type_real = "[REAL]",
    .type_cache = "[CACHE]",
    .type_block = "[BLOCK]",
    .warn_swap = "⚠️  SWAP on monitored disk",
    .warn_atime = "⚠️  ATIME updates detected",
    .ghost_io = "[GHOST] Disk activity without file events",
    .report_title = "Final Analysis Report",
    .suspect = "Suspect Ranking",
    .host_label = "[Host]",
    .container_label = "[Container:%s]",
    .confidence = "Confidence",
    .standby = "[STANDBY] Idle for %ds",
    .wakeup = "[WAKEUP] Disk activity @ %s",
    .attributed = "attributed to",
    .unattributed = "unattributed",
    .waiting_start = "Waiting for start time...",
    .duration_expired = "Duration expired, exiting...",
    .daemon_mode = "Daemon mode",
    .log_rotated = "Log rotated to: %s",
    .quiet_mode = "Quiet mode (screen output disabled)",
    .help_text =
    "IOWATCH v" VERSION " - Physical Disk Sleep Analyzer\n"
    "==================================================================\n"
    "Usage: sudo %s [OPTIONS] <MOUNT_POINT>\n"
    "\nOptions:\n"
    "  -L <cn|en>           Language (cn=Chinese, en=English, default=en)\n"
    "  -d, -v               Enable debug mode\n"
    "  -o <file>            CSV log file (NOT on monitored disk!)\n"
    "  -D, --daemon         Daemon mode (requires -o)\n"
    "  -s <time>            Scheduled start: +10m, +2h, or 202601011200\n"
    "  -e <duration>        Run duration: 30m, 1h, 2d\n"
    "  -r <interval>        Log rotation: 1h, 30m\n"
    "  -q, --quiet          Quiet mode\n"
    "  -t                   Test disk detection only\n"
    "  --pid <PID>          Monitor only specified PID\n"
    "  --process <name>     Monitor only specified process\n"
    "  --help               Show this help\n"
    "\nSupported: Ext4/XFS/ZFS/Btrfs/LVM/MD RAID/NVMe/eMMC\n",
    /* 新增诊断字段 */
    .diag_header = "📊 IOWatch Intelligent Diagnostic Report",
    .env_diag = "Environment Diagnosis",
    .advice_header = "💡 Smart Suggestions",
    .advice_atime = "Detected frequent atime updates. Add 'noatime' to mount options to reduce metadata writes.",
    .advice_docker = "Detected Docker IO on HDD. Move Docker root to SSD/RAM disk for better performance.",
    .advice_commit = "Frequent jbd2 activity. Increase 'commit=60' mount option to lower journal sync frequency.",
    .advice_swap = "High swap activity (kswapd0). Increase RAM or lower vm.swappiness.",
    .advice_log = "Frequent log writes. Use log2ram to store logs in tmpfs."
};

static const lang_t lang_cn = {
    .title = "IOWATCH v" VERSION " (智能诊断版)",
    .mon_start = "监控已启动",
    .disks = "底层物理磁盘",
    .header_title = "%-23s %-7s %-7s %-17s %-7s %-17s %-8s %s %s\n",
    .data_format = "%-23s %-7s PID:%-8d [%-15s] <- PPID:%-8d [%-15s] %-8s %s %s\n",
    .type_real = "[真实]",
    .type_cache = "[缓存]",
    .type_block = "[块层]",
    .warn_swap = "⚠️  交换分区在监控磁盘上",
    .warn_atime = "⚠️  检测到访问时间(atime)更新",
    .ghost_io = "[幽灵] 磁盘产生位移但无文件操作",
    .report_title = "最终分析报告",
    .suspect = "嫌疑进程排名",
    .host_label = "[宿主机]",
    .container_label = "[容器:%s]",
    .confidence = "置信度",
    .standby = "[待机] 空闲 %d 秒",
    .wakeup = "[唤醒] 磁盘活动 @ %s",
    .attributed = "归因于",
    .unattributed = "未归因",
    .waiting_start = "等待启动时间...",
    .duration_expired = "运行时长已到，退出...",
    .daemon_mode = "后台运行模式",
    .log_rotated = "日志已滚动到: %s",
    .quiet_mode = "静默模式 (屏幕输出已禁用)",
    .help_text =
    "IOWATCH v" VERSION " - 物理磁盘休眠分析工具\n"
    "==================================================================\n"
    "用法: sudo %s [选项] <挂载点>\n"
    "\n选项:\n"
    "  -L <cn|en>           语言 (cn=中文, en=英文, 默认=en)\n"
    "  -d, -v               开启调试模式\n"
    "  -o <文件>            CSV日志文件 (必须不在监控盘上!)\n"
    "  -D, --daemon         后台运行模式 (需要 -o)\n"
    "  -s <时间>            定时启动: +10m, +2h, 或 202601011200\n"
    "  -e <时长>            运行时长: 30m, 1h, 2d\n"
    "  -r <间隔>            日志滚动间隔: 1h, 30m\n"
    "  -q, --quiet          静默模式\n"
    "  -t                   仅测试磁盘识别\n"
    "  --pid <PID>          只监控指定PID\n"
    "  --process <名称>     只监控指定进程\n"
    "  --help               显示本帮助\n"
    "\n支持: Ext4/XFS/ZFS/Btrfs/LVM/MD RAID/NVMe/eMMC\n",
    /* 新增诊断字段 */
    .diag_header = "📊 IOWatch 智能诊断报告",
    .env_diag = "环境诊断",
    .advice_header = "💡 智能优化建议",
    .advice_atime = "检测到频繁atime更新。添加noatime挂载参数可减少元数据写入。",
    .advice_docker = "检测到HDD上的Docker IO。建议将Docker根目录迁移至SSD/内存盘。",
    .advice_commit = "频繁jbd2活动。增加commit=60挂载参数可降低日志同步频率。",
    .advice_swap = "检测到Swap交换活动(kswapd0)。建议增加内存或调低vm.swappiness。",
    .advice_log = "频繁日志写入。建议使用log2ram将日志暂存于内存。"
};

static const lang_t *g_msg = &lang_en;

/* ==================== 基础工具函数==================== */
static void safe_strncpy(char *d, const char *s, size_t n) {
    if (d && s) { strncpy(d, s, n); d[n-1] = '\0'; }
}

static int cmd_exists(const char *cmd) {
    char path[PATH_MAX];
    const char *paths[] = {"/bin/", "/usr/bin/", "/sbin/", "/usr/sbin/", ""};
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s%s", paths[i], cmd);
        if (access(path, X_OK) == 0) return 1;
    }
    return 0;
}

static void csv_escape(FILE *f, const char *str) {
    if (!f || !str) return;
    fputc('"', f);
    for (const char *p = str; *p; p++) {
        if (*p == '"') fputs("\"\"", f);
        else if (*p == '\n' || *p == '\r') fputc(' ', f);
        else fputc(*p, f);
    }
    fputc('"', f);
}

static void printf_lang(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (!quiet_mode || daemon_mode) {
        vprintf(fmt, args);
        fflush(stdout);
    }
    va_end(args);
}

static void debug_printf(const char *fmt, ...) {
    if (!debug_mode) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}

/* ==================== 环境自发现==================== */
static void env_probe(const char *mnt) {
    struct statfs stfs;
    if (statfs(mnt, &stfs) == 0) {
        switch (stfs.f_type) {
            case 0xef53:       g_fs_type = 1; break;
            case 0x58465342:   g_fs_type = 2; break;
            case 0x2fc17f13:   g_fs_type = 3; break;
            case 0x9123683e:   g_fs_type = 4; break;
        }
        debug_printf("[DEBUG] Filesystem type: %d\n", g_fs_type);
    }

    FILE *mf = fopen("/proc/self/mountinfo", "r");
    if (!mf) return;
    char line[8192];
    while (fgets(line, sizeof(line), mf)) {
        char *sep = strstr(line, " - ");
        if (!sep) continue;
        char *mp_start = sep - 1;
        while (mp_start > line && *mp_start == ' ') mp_start--;
        while (mp_start > line && *mp_start != ' ') mp_start--;
        if (*mp_start == ' ') mp_start++;
        size_t mp_len = sep - 1 - mp_start;
        if (mp_len != strlen(mnt) || strncmp(mp_start, mnt, mp_len) != 0 || mnt[mp_len] != '\0')
            continue;

        char *col = line;
        for (int i = 0; i < 5; i++) {
            col = strchr(col + 1, ' ');
            while (*col == ' ') col++;
        }
        char *opts_end = strchr(col, ' ');
        if (opts_end) {
            char opts[512] = {0};
            strncpy(opts, col, opts_end - col);
            if (strstr(opts, "noatime")) g_has_noatime = 1;
        }

        if (g_fs_type == 3) {
            for (int i = 0; i < 4; i++) {
                col = strchr(col + 1, ' ');
                while (*col == ' ') col++;
            }
            char *pool_end = strchr(col, ' ');
            if (pool_end) {
                strncpy(g_zfs_pool, col, pool_end - col);
                g_zfs_pool[sizeof(g_zfs_pool)-1] = '\0';
                char txg_path[256];
                snprintf(txg_path, sizeof(txg_path), "/proc/spl/kstat/zfs/%s/txgs", g_zfs_pool);
                if (access(txg_path, R_OK) == 0) {
                    g_zfs_txg_enabled = 1;
                    debug_printf("[DEBUG] ZFS TXG attribution enabled (pool: %s)\n", g_zfs_pool);
                } else {
                    debug_printf("[DEBUG] ZFS txgs interface not found, fallback to basic mode\n");
                }
            }
        }
        break;
    }
    fclose(mf);

    char docker_path[PATH_MAX];
    snprintf(docker_path, sizeof(docker_path), "%s/overlay2", mnt);
    if (access(docker_path, F_OK) == 0) g_has_docker = 1;
    snprintf(docker_path, sizeof(docker_path), "%s/docker", mnt);
    if (access(docker_path, F_OK) == 0) g_has_docker = 1;

    if (g_disk_cnt > 0) {
        char rot_path[PATH_MAX];
        snprintf(rot_path, sizeof(rot_path), "/sys/block/%s/queue/rotational", g_disks[0].name);
        int rot_fd = open(rot_path, O_RDONLY);
        if (rot_fd >= 0) {
            char rot_buf[4] = {0};
            ssize_t dummy = read(rot_fd, rot_buf, sizeof(rot_buf)); (void)dummy;
            g_is_hdd = (rot_buf[0] == '1');
            close(rot_fd);
        }
    }
    debug_printf("[DEBUG] Env probe done: noatime=%d, docker=%d, hdd=%d\n",
                 g_has_noatime, g_has_docker, g_is_hdd);
}

/* ==================== 磁盘识别逻辑==================== */
static int is_physical_disk(const char *dev) {
    if (!dev || !dev[0]) return 0;
    if (strncmp(dev, "dm-", 3) == 0) return 0;
    if (strncmp(dev, "md", 2) == 0 && isdigit(dev[2])) return 0;
    if (strncmp(dev, "loop", 4) == 0) return 0;
    if (strncmp(dev, "zram", 4) == 0) return 0;
    if (strncmp(dev, "bcache", 6) == 0) return 0;
    if (strncmp(dev, "rbd", 3) == 0) return 0;
    if (strncmp(dev, "nbd", 3) == 0) return 0;
    if (strncmp(dev, "drbd", 4) == 0) return 0;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/block/%s", dev);
    if (access(path, F_OK) != 0) return 0;

    if (strncmp(dev, "sd", 2) == 0 && isalpha(dev[2])) return 1;
    int ctrl, ns;
    if (sscanf(dev, "nvme%dn%d", &ctrl, &ns) == 2) return 1;
    if (strncmp(dev, "mmcblk", 6) == 0 && isdigit(dev[6])) return 1;
    if (strncmp(dev, "vd", 2) == 0 && isalpha(dev[2])) return 1;
    if (strncmp(dev, "hd", 2) == 0 && isalpha(dev[2])) return 1;
    return 0;
}

static int is_partition(const char *dev) {
    if (!dev || !dev[0]) return 0;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/sys/class/block/%s/partition", dev);
    return access(path, F_OK) == 0;
}

static void add_physical_disk(const char *disk) {
    if (!disk || !disk[0] || !is_physical_disk(disk)) return;
    for (int i = 0; i < g_disk_cnt; i++)
        if (strcmp(g_disks[i].name, disk) == 0) return;
    if (g_disk_cnt >= MAX_DISKS) return;
    safe_strncpy(g_disks[g_disk_cnt].name, disk, 32);
    if (g_disk_cnt > 0) strcat(disk_names, ", ");
    strcat(disk_names, disk);
    g_disk_cnt++;
}

static void resolve_disk_recursive(const char *dev, int depth) {
    if (depth > MAX_RECURSE_DEPTH || !dev || !dev[0]) return;
    char slaves_path[PATH_MAX];
    snprintf(slaves_path, sizeof(slaves_path), "/sys/class/block/%s/slaves", dev);
    DIR *d = opendir(slaves_path);
    if (d) {
        struct dirent *en;
        int has_slave = 0;
        while ((en = readdir(d))) {
            if (en->d_name[0] == '.') continue;
            has_slave = 1;
            resolve_disk_recursive(en->d_name, depth + 1);
        }
        closedir(d);
        if (has_slave) return;
    }
    if (is_partition(dev)) {
        char parent_link[PATH_MAX];
        snprintf(parent_link, sizeof(parent_link), "/sys/class/block/%s/..", dev);
        char *real_path = realpath(parent_link, NULL);
        if (real_path) {
            char *p = strrchr(real_path, '/');
            if (p && p[1]) resolve_disk_recursive(p + 1, depth + 1);
            free(real_path);
        }
        return;
    }
    if (is_physical_disk(dev)) add_physical_disk(dev);
}

static int get_mount_dev_from_mountinfo(const char *mnt, char *kernel_name, size_t sz) {
    FILE *f = NULL;
    char line[8192];
    char dev_path[PATH_MAX] = {0};

    f = fopen("/proc/self/mountinfo", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char *sep = strstr(line, " - ");
            if (!sep) continue;

            char *mp_end = sep - 1;
            while (mp_end > line && *mp_end == ' ') mp_end--;
            char *mp_start = mp_end;
            while (mp_start > line && *mp_start != ' ') mp_start--;
            if (*mp_start == ' ') mp_start++;

            size_t mp_len = mp_end - mp_start + 1;
            if (mp_len < 2) continue;

            while (mp_len > 0 && mp_start[mp_len-1] == '/') mp_len--;
            if (mp_len != strlen(mnt) || strncmp(mp_start, mnt, mp_len) != 0 || mnt[mp_len] != '\0')
                continue;

            if (sscanf(sep + 3, "%*s %4095s", dev_path) != 1) continue;
            debug_printf("[DEBUG] Mountinfo matched: mount_point=%.*s, device=%s\n", (int)mp_len, mp_start, dev_path);
            break;
        }
        fclose(f);
    }

    if (dev_path[0] == '\0' && cmd_exists("findmnt")) {
        debug_printf("[DEBUG] Mountinfo failed, try findmnt\n");
        char cmd[PATH_MAX];
        snprintf(cmd, sizeof(cmd), "findmnt -n -o SOURCE --target '%s'", mnt);
        f = popen(cmd, "r");
        if (f) {
            char buf[PATH_MAX];
            if (fgets(buf, sizeof(buf), f)) {
                buf[strcspn(buf, "\n")] = '\0';
                strncpy(dev_path, buf, sizeof(dev_path)-1);
                debug_printf("[DEBUG] Findmnt got dev path: %s\n", dev_path);
            }
            pclose(f);
        }
    }

    if (dev_path[0] != '\0') {
        struct stat st;
        if (stat(dev_path, &st) == 0 && S_ISBLK(st.st_mode)) {
            char sys_path[PATH_MAX];
            snprintf(sys_path, sizeof(sys_path), "/sys/dev/block/%u:%u",
                     (unsigned int)major(st.st_rdev), (unsigned int)minor(st.st_rdev));
            char *resolved = realpath(sys_path, NULL);
            if (resolved) {
                char *name = strrchr(resolved, '/');
                if (name && name[1]) {
                    safe_strncpy(kernel_name, name + 1, sz);
                    debug_printf("[DEBUG] Converted to kernel name: %s\n", kernel_name);
                    free(resolved);
                    return 0;
                }
                free(resolved);
            }
        }
        char *name = strrchr(dev_path, '/');
        if (name && name[1]) {
            safe_strncpy(kernel_name, name + 1, sz);
            return 0;
        }
    }

    debug_printf("[DEBUG] Failed to get kernel name for %s\n", mnt);
    return -1;
}

static void init_sys(const char *mnt) {
    memset(disk_names, 0, sizeof(disk_names));
    g_disk_cnt = 0;
    char kernel_name[PATH_MAX] = {0};
    if (get_mount_dev_from_mountinfo(mnt, kernel_name, sizeof(kernel_name)) == 0)
        resolve_disk_recursive(kernel_name, 0);
    FILE *f = fopen("/proc/swaps", "r");
    if (f) {
        char line[256]; int first = 1;
        while (fgets(line, 256, f)) {
            if (first) { first = 0; continue; }
            char dev[64]; sscanf(line, "%63s", dev);
            char *p = strrchr(dev, '/');
            char *dn = p ? p + 1 : dev;
            for (int i = 0; i < g_disk_cnt; i++)
                if (strstr(dn, g_disks[i].name) || strstr(g_disks[i].name, dn))
                    { g_swap_on_disk = 1; safe_strncpy(g_swap_dev, dn, 64); break; }
        }
        fclose(f);
    }
}

/* ==================== 进程/Inode管理==================== */
static proc_info_t *get_proc(pid_t pid) {
    pthread_mutex_lock(&cache_mtx);
    unsigned int h = (unsigned int)pid % MAX_PID_CACHE;
    int idx = p_hash[h];
    if (idx >= 0 && idx < MAX_PID_CACHE && p_cache[idx].pid == pid && p_cache[idx].valid) {
        pthread_mutex_unlock(&cache_mtx);
        return &p_cache[idx];
    }
    int target = -1;
    for (int i = 0; i < p_count; i++)
        if (p_cache[i].pid == pid && p_cache[i].valid) { target = i; break; }
    if (target == -1) target = (p_count < MAX_PID_CACHE) ? p_count++ : (int)h;

    proc_info_t *p = &p_cache[target];
    memset(p, 0, sizeof(proc_info_t));
    p->pid = pid; p->valid = 1; p->cache_t = time(NULL); p_hash[h] = target;
    p->ppid = -1; safe_strncpy(p->parent_comm, "?", sizeof(p->parent_comm));

    char path[256]; snprintf(path, 256, "/proc/%d/comm", pid);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, p->comm, 15); (void)n;
        p->comm[strcspn(p->comm, "\n")] = '\0';
        close(fd);
    } else strcpy(p->comm, "?");

    snprintf(path, 256, "/proc/%d/status", pid);
    FILE *ff = fopen(path, "r");
    if (ff) {
        char line[256];
        while (fgets(line, 256, ff))
            if (strncmp(line, "PPid:", 5) == 0) { sscanf(line, "PPid:\t%d", &p->ppid); break; }
        fclose(ff);
    }
    if (p->ppid > 0) {
        snprintf(path, 256, "/proc/%d/comm", p->ppid);
        fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, p->parent_comm, 15); (void)n;
            p->parent_comm[strcspn(p->parent_comm, "\n")] = '\0';
            close(fd);
        }
    }
    if (strstr(p->comm, "flush") || strstr(p->comm, "jbd2") || strstr(p->comm, "kworker") || strstr(p->comm, "kswapd"))
        { p->is_kthread = 1; safe_strncpy(p->ktype, p->comm, 32); }
    snprintf(path, 256, "/proc/%d/io", pid);
    ff = fopen(path, "r");
    if (ff) {
        char line[256];
        while (fgets(line, 256, ff)) {
            if (strncmp(line, "read_bytes:", 11) == 0) sscanf(line, "read_bytes: %lu", &p->r_snap);
            else if (strncmp(line, "write_bytes:", 12) == 0) sscanf(line, "write_bytes: %lu", &p->w_snap);
        }
        fclose(ff);
    }
    stats_t *st = &p_stats[target];
    char cg_path[256]; snprintf(cg_path, 256, "/proc/%d/cgroup", pid);
    FILE *cg = fopen(cg_path, "r");
    if (cg) {
        char line[256];
        while (fgets(line, 256, cg)) {
            if (strstr(line, "docker") || strstr(line, "containerd") || strstr(line, "kubepods")) {
                char *p = strchr(line, '/');
                while (p && *p) {
                    if (strstr(p, "docker-") || strstr(p, "containerd-")) {
                        char *end = strchr(p, '.');
                        if (end) *end = '\0';
                        safe_strncpy(st->container, p, CGROUP_PATH_MAX);
                        break;
                    }
                    p = strchr(p + 1, '/');
                }
                break;
            }
        }
        fclose(cg);
    }
    st->first_seen = time(NULL);
    pthread_mutex_unlock(&cache_mtx);
    return p;
}

static void add_inode_owner(ino_t inode, dev_t dev, pid_t pid, const char *comm) {
    if (inode == 0 || !comm) return;
    pthread_mutex_lock(&cache_mtx);
    int idx = -1;
    for (int i = 0; i < inode_hist_count; i++)
        if (inode_hist[i].inode == inode && inode_hist[i].dev == dev) { idx = i; break; }
    if (idx == -1 && inode_hist_count < INODE_CACHE_SIZE) {
        idx = inode_hist_count++;
        inode_hist[idx].inode = inode;
        inode_hist[idx].dev = dev;
        inode_hist[idx].count = 0;
    }
    if (idx != -1) {
        inode_history_t *h = &inode_hist[idx];
        int slot = -1;
        for (int i = 0; i < MAX_OWNERS_PER_INODE; i++) {
            if (h->owners[i].pid == pid && h->owners[i].active) { slot = i; break; }
            if (!h->owners[i].active && slot == -1) slot = i;
        }
        if (slot == -1) {
            int oldest = 0;
            for (int i = 1; i < MAX_OWNERS_PER_INODE; i++)
                if (h->owners[i].dirty_time < h->owners[oldest].dirty_time) oldest = i;
            slot = oldest;
        }
        h->owners[slot].pid = pid;
        safe_strncpy(h->owners[slot].comm, comm, 16);
        h->owners[slot].dirty_time = time(NULL);
        h->owners[slot].dirty_bytes = 0;
        h->owners[slot].active = 1;
        if (slot >= h->count) h->count = slot + 1;
    }
    pthread_mutex_unlock(&cache_mtx);
}

static void update_dirty_bytes(ino_t inode, dev_t dev, pid_t pid, uint64_t delta) {
    if (inode == 0 || delta == 0) return;
    pthread_mutex_lock(&cache_mtx);
    for (int i = 0; i < inode_hist_count; i++) {
        if (inode_hist[i].inode == inode && inode_hist[i].dev == dev) {
            for (int j = 0; j < inode_hist[i].count; j++) {
                if (inode_hist[i].owners[j].pid == pid && inode_hist[i].owners[j].active) {
                    inode_hist[i].owners[j].dirty_bytes += delta;
                    inode_hist[i].owners[j].dirty_time = time(NULL);
                    break;
                }
            }
            break;
        }
    }
    pthread_mutex_unlock(&cache_mtx);
}

static int calc_confidence(inode_history_t *h, struct timespec *block_ts, time_t now) {
    if (!h) return 0;
    int best = 0;
    for (int i = 0; i < h->count; i++) {
        if (!h->owners[i].active) continue;
        int score = 0;
        long diff = now - h->owners[i].dirty_time;
        if (diff < 5) score += 50;
        else if (diff < 10) score += 40;
        else if (diff < 30) score += 25;
        else if (diff < 60) score += 10;
        if (h->owners[i].dirty_bytes > 1024 * 1024) score += 20;
        if (h->owners[i].dirty_bytes > 10 * 1024 * 1024) score += 30;
        if (strstr(h->owners[i].comm, "mysql") || strstr(h->owners[i].comm, "postgres") || strstr(h->owners[i].comm, "redis"))
            score += 15;
        if (score > best) best = score;
    }
    return best > 100 ? 100 : best;
}

/* ==================== Block Trace解析==================== */
static void parse_block_line(char *line, block_event_t *ev) {
    if (!line || !ev) return;
    memset(ev, 0, sizeof(block_event_t));
    clock_gettime(CLOCK_REALTIME, &ev->ts);
    char *issue = strstr(line, "block_rq_issue:");
    if (!issue) return;
    char *p = issue + 15;
    while (*p == ' ') p++;
    unsigned int maj = 0, min = 0;
    if (sscanf(p, "%u:%u", &maj, &min) != 2 && sscanf(p, "%u,%u", &maj, &min) != 2) return;
    while (*p && *p != 'W' && *p != 'R' && *p != 'D') p++;
    if (!*p) return;
    ev->write = (*p == 'W' || *p == 'D');
    while (*p && !isdigit(*p)) p++;
    if (!*p) return;
    unsigned long long sector = 0;
    unsigned int nr = 0;
    if (sscanf(p, "%llu + %u", &sector, &nr) >= 1) {
        ev->sector = sector;
        ev->bytes = nr * 512;
    }
    char sys_path[PATH_MAX];
    snprintf(sys_path, sizeof(sys_path), "/sys/dev/block/%u:%u", maj, min);
    char *resolved = realpath(sys_path, NULL);
    if (resolved) {
        char *name = strrchr(resolved, '/');
        if (name && name[1]) safe_strncpy(ev->disk, name + 1, sizeof(ev->disk));
        free(resolved);
    } else {
        snprintf(ev->disk, sizeof(ev->disk), "%u:%u", maj, min);
    }
    char *bracket = strchr(line, '[');
    if (bracket) {
        char *start = line;
        while (start < bracket && *start == ' ') start++;
        char comm[32] = {0};
        size_t len = bracket - start;
        if (len < sizeof(comm)) {
            strncpy(comm, start, len);
            comm[len] = '\0';
            safe_strncpy(ev->comm, comm, sizeof(ev->comm));
            char *dash = strrchr(comm, '-');
            if (dash && *(dash + 1)) ev->pid = atoi(dash + 1);
        }
    }
}

static void correlate_block(block_event_t *ev) {
    if (!ev) return;
    time_t now = time(NULL);
    int best_score = 0;
    inode_history_t *best_h = NULL;
    int best_owner_idx = -1;

    int is_metadata = 0;
    pid_t meta_pid = -1;
    char meta_comm[16] = {0};
    pthread_mutex_lock(&cache_mtx);
    if ((strncmp(ev->comm, "jbd2/", 5) == 0 || strcmp(ev->comm, "xfsaild") == 0 || strncmp(ev->comm, "kworker", 7) == 0) &&
        now - g_shadow.last_ts <= 2) {
        is_metadata = 1;
        meta_pid = g_shadow.user_pid;
        safe_strncpy(meta_comm, g_shadow.user_comm, sizeof(meta_comm));
    }
    pthread_mutex_unlock(&cache_mtx);

    pthread_mutex_lock(&cache_mtx);
    for (int i = 0; i < inode_hist_count; i++) {
        inode_history_t *h = &inode_hist[i];
        int sc = calc_confidence(h, &ev->ts, now);
        if (sc > best_score) {
            best_score = sc;
            best_h = h;
            int best_owner = -1;
            for (int j = 0; j < h->count; j++) {
                if (!h->owners[j].active) continue;
                int o_score = 0;
                long diff = now - h->owners[j].dirty_time;
                if (diff < 5) o_score += 50;
                else if (diff < 10) o_score += 40;
                else if (diff < 30) o_score += 25;
                else if (diff < 60) o_score += 10;
                if (h->owners[j].dirty_bytes > 1024 * 1024) o_score += 20;
                if (o_score > best_score) best_owner = j;
            }
            best_owner_idx = best_owner;
        }
    }
    pthread_mutex_unlock(&cache_mtx);

    char ts[32]; struct tm tm; localtime_r(&ev->ts.tv_sec, &tm); strftime(ts, 32, "%Y-%m-%d %H:%M:%S", &tm);
    if (best_h && best_score > 30 && best_owner_idx >= 0) {
        printf_lang("[%s.%03ld] [BLOCK] %s %s sec:%llu bytes:%llu -> %s (PID %d) %s: %d%%\n",
                    ts, ev->ts.tv_nsec / 1000000, ev->write ? "WRITE" : "READ", ev->disk,
                    (unsigned long long)ev->sector, (unsigned long long)ev->bytes,
                    best_h->owners[best_owner_idx].comm, best_h->owners[best_owner_idx].pid,
                    g_msg->confidence, best_score);
    } else if (is_metadata && meta_pid > 0) {
        printf_lang("[%s.%03ld] [BLOCK] %s %s sec:%llu bytes:%llu (%s: likely caused by %s PID:%d)\n",
                    ts, ev->ts.tv_nsec / 1000000, ev->write ? "WRITE" : "READ", ev->disk,
                    (unsigned long long)ev->sector, (unsigned long long)ev->bytes,
                    g_msg->type_block, meta_comm, meta_pid);
    } else {
        printf_lang("[%s.%03ld] [BLOCK] %s %s sec:%llu bytes:%llu (%s)\n",
                    ts, ev->ts.tv_nsec / 1000000, ev->write ? "WRITE" : "READ", ev->disk,
                    (unsigned long long)ev->sector, (unsigned long long)ev->bytes, g_msg->unattributed);
    }
}

/* ==================== 线程逻辑==================== */
static int open_best_trace_pipe(void) {
    int ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) ncpu = 4;
    for (int cpu = 0; cpu < ncpu; cpu++) {
        char path[256];
        snprintf(path, 256, TRACEFS_PERCPU "%d/trace_pipe", cpu);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) return fd;
    }
    int fd = open("/sys/kernel/debug/tracing/trace_pipe", O_RDONLY | O_NONBLOCK);
    if (fd >= 0) return fd;
    return open("/sys/kernel/tracing/trace_pipe", O_RDONLY | O_NONBLOCK);
}

static void *trace_reader_thread(void *arg) {
    (void)arg;
    char buf[8192];
    while (!exit_flag) {
        fd_set fds; FD_ZERO(&fds); FD_SET(trace_fd, &fds);
        struct timeval tv = {1, 0};
        if (select(trace_fd + 1, &fds, NULL, NULL, &tv) <= 0) continue;
        ssize_t n = read(trace_fd, buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        char *line = buf, *next;
        while ((next = strstr(line, "\n")) != NULL) {
            *next = '\0';
            block_event_t ev;
            parse_block_line(line, &ev);
            pthread_mutex_lock(&block_mtx);
            if (b_ring.count < BLOCK_RING_SIZE) {
                b_ring.buffer[b_ring.tail % BLOCK_RING_SIZE] = ev;
                b_ring.tail++;
                b_ring.count++;
                pthread_cond_signal(&block_cnd);
            }
            pthread_mutex_unlock(&block_mtx);
            line = next + 1;
        }
    }
    return NULL;
}

static void *block_consumer_thread(void *arg) {
    (void)arg;
    while (!exit_flag) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;
        pthread_mutex_lock(&block_mtx);
        if (b_ring.count == 0) {
            pthread_cond_timedwait(&block_cnd, &block_mtx, &timeout);
            if (b_ring.count == 0) { pthread_mutex_unlock(&block_mtx); continue; }
        }
        block_event_t ev = b_ring.buffer[b_ring.head % BLOCK_RING_SIZE];
        b_ring.head++;
        b_ring.count--;
        pthread_mutex_unlock(&block_mtx);
        correlate_block(&ev);
    }
    return NULL;
}

static int read_diskstats(void) {
    FILE *f = fopen("/proc/diskstats", "r");
    if (!f) return 0;
    int moved = 0;
    char ln[512], nm[64];
    while (fgets(ln, 512, f)) {
        unsigned long long r, w;
        if (sscanf(ln, "%*u %*u %63s %*u %*u %llu %*u %*u %*u %llu", nm, &r, &w) == 3) {
            for (int i = 0; i < g_disk_cnt; i++)
                if (strcmp(nm, g_disks[i].name) == 0) {
                    if (g_disks[i].last_total > 0 && (r + w) > g_disks[i].last_total)
                        moved = 1;
                    g_disks[i].last_total = r + w;
                }
        }
    }
    fclose(f);
    return moved;
}

static void confirm_history(void) {
    int real = read_diskstats();
    pthread_mutex_lock(&ring_mtx);
    int todo = g_ring.count;
    pthread_mutex_unlock(&ring_mtx);

    for (int i = 0; i < todo; i++) {
        history_entry_t ev;
        pthread_mutex_lock(&ring_mtx);
        ev = g_ring.buffer[g_ring.head % RING_BUFFER_SIZE];
        g_ring.head++;
        g_ring.count--;
        pthread_mutex_unlock(&ring_mtx);

        if (!ev.st_ptr) continue;
        stats_t *st = ev.st_ptr;
        st->events++;

        if (ev.mask & (FAN_MODIFY | FAN_CLOSE_WRITE)) {
            add_inode_owner(ev.inode, ev.dev, ev.pid, ev.comm_cache);

            if (g_fs_type == 3 && g_zfs_txg_enabled) {
                pthread_mutex_lock(&cache_mtx);
                if (g_zfs_pending_cnt < ZFS_PENDING_MAX) {
                    g_zfs_pending[g_zfs_pending_cnt].ev = ev;
                    g_zfs_pending[g_zfs_pending_cnt].enqueue_ts = time(NULL);
                    g_zfs_pending[g_zfs_pending_cnt].is_pending = 1;
                    g_zfs_pending_cnt++;
                    debug_printf("[DEBUG] ZFS event added to pending queue (total: %d)\n", g_zfs_pending_cnt);
                }
                pthread_mutex_unlock(&cache_mtx);
                continue;
            }

            char path[256]; snprintf(path, 256, "/proc/%d/io", ev.pid);
            FILE *f = fopen(path, "r");
            if (f) {
                uint64_t cw = 0;
                char line[256];
                while (fgets(line, 256, f))
                    if (strncmp(line, "write_bytes:", 12) == 0) { sscanf(line, "write_bytes: %lu", &cw); break; }
                fclose(f);
                if (cw > 0) {
                    pthread_mutex_lock(&cache_mtx);
                    for (int j = 0; j < p_count; j++) {
                        if (p_cache[j].pid == ev.pid && p_cache[j].valid) {
                            if (cw > p_cache[j].w_snap) {
                                uint64_t delta = cw - p_cache[j].w_snap;
                                p_cache[j].w_snap = cw;
                                update_dirty_bytes(ev.inode, ev.dev, ev.pid, delta);
                            }
                            break;
                        }
                    }
                    pthread_mutex_unlock(&cache_mtx);
                }
            }
        }

        if (real) {
            st->real_io++;
            st->last_real_io = time(NULL);
            char ts[32]; struct tm tm; localtime_r(&ev.ts.tv_sec, &tm); strftime(ts, 32, "%Y-%m-%d %H:%M:%S", &tm);
            char cinfo[CONTAINER_INFO_LEN] = {0};
            if (st->container[0]) {
                if (g_msg == &lang_cn)
                    snprintf(cinfo, sizeof(cinfo), "[容器:%s]", st->container);
                else
                    snprintf(cinfo, sizeof(cinfo), "[Container:%s]", st->container);
            } else {
                safe_strncpy(cinfo, g_msg->host_label, sizeof(cinfo));
            }
            const char *act = (ev.mask & FAN_MODIFY) ? "WRITE" : (ev.mask & FAN_OPEN) ? "OPEN" : "ACCESS";
            printf_lang(g_msg->data_format, ts, act, ev.pid, ev.comm_cache, ev.ppid_cache, ev.parent_comm_cache, g_msg->type_real, ev.path, cinfo);
            if (csv_stream) {
                fprintf(csv_stream, "%s.%03ld,%s,%d,%s,%d,%s,", ts, ev.ts.tv_nsec / 1000000, act, ev.pid, ev.comm_cache, ev.ppid_cache, ev.parent_comm_cache);
                csv_escape(csv_stream, ev.path);
                fprintf(csv_stream, ",%d,", 1);
                csv_escape(csv_stream, st->container);
                fprintf(csv_stream, ",%lu\n", (unsigned long)ev.inode);
            }
        } else {
            st->cache_hit++;
        }
        st->last_seen = time(NULL);
    }

    time_t now = time(NULL);
    if (real) {
        if (g_state == 2) {
            char wake_ts[32]; struct tm tm; localtime_r(&now, &tm); strftime(wake_ts, 32, "%Y-%m-%d %H:%M:%S", &tm);
            printf_lang(g_msg->wakeup, wake_ts);
        }
        g_state = 1;
        idle_t = now;
    } else if (g_state != 2 && (now - idle_t >= IDLE_THRESHOLD)) {
        g_state = 2;
        printf_lang(g_msg->standby, IDLE_THRESHOLD);
    }
}

static void *cons_func(void *arg) {
    (void)arg;
    struct timespec last; clock_gettime(CLOCK_REALTIME, &last);
    time_t last_txg_check = 0;
    while (!exit_flag) {
        struct timespec now; clock_gettime(CLOCK_REALTIME, &now);
        long ms = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
        if (ms >= SAMPLE_INTERVAL_MS) {
            confirm_history();

            if (g_zfs_txg_enabled && now.tv_sec - last_txg_check >= 1) {
                last_txg_check = now.tv_sec;
                char txg_path[256];
                snprintf(txg_path, sizeof(txg_path), "/proc/spl/kstat/zfs/%s/txgs", g_zfs_pool);
                FILE *tf = fopen(txg_path, "r");
                if (tf) {
                    char line[256];
                    uint64_t sync_done = 0;
                    while (fgets(line, sizeof(line), tf)) {
                        if (sscanf(line, "sync_done %lu", &sync_done) == 1) break;
                    }
                    fclose(tf);
                    if (sync_done > g_zfs_last_sync_done) {
                        pthread_mutex_lock(&cache_mtx);
                        time_t cutoff = now.tv_sec - 10;
                        for (int i = 0; i < g_zfs_pending_cnt; i++) {
                            if (g_zfs_pending[i].is_pending && g_zfs_pending[i].enqueue_ts >= cutoff) {
                                history_entry_t *pev = &g_zfs_pending[i].ev;
                                stats_t *st = pev->st_ptr;
                                if (st) {
                                    st->real_io++;
                                    st->last_real_io = now.tv_sec;
                                    char path[256]; snprintf(path, 256, "/proc/%d/io", pev->pid);
                                    FILE *f = fopen(path, "r");
                                    if (f) {
                                        uint64_t cw = 0;
                                        char line[256];
                                        while (fgets(line, 256, f)) {
                                            if (strncmp(line, "write_bytes:", 12) == 0) {
                                                sscanf(line, "write_bytes: %lu", &cw);
                                                break;
                                            }
                                        }
                                        fclose(f);
                                        if (cw > 0) {
                                            for (int j = 0; j < p_count; j++) {
                                                if (p_cache[j].pid == pev->pid && p_cache[j].valid) {
                                                    if (cw > p_cache[j].w_snap) {
                                                        uint64_t delta = cw - p_cache[j].w_snap;
                                                        p_cache[j].w_snap = cw;
                                                        update_dirty_bytes(pev->inode, pev->dev, pev->pid, delta);
                                                    }
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                                g_zfs_pending[i].is_pending = 0;
                                debug_printf("[DEBUG] ZFS pending event committed (PID: %d)\n", pev->pid);
                            }
                        }
                        int new_cnt = 0;
                        for (int i = 0; i < g_zfs_pending_cnt; i++) {
                            if (g_zfs_pending[i].is_pending)
                                g_zfs_pending[new_cnt++] = g_zfs_pending[i];
                        }
                        g_zfs_pending_cnt = new_cnt;
                        g_zfs_last_sync_done = sync_done;
                    }
                } else {
                    g_zfs_txg_enabled = 0;
                    debug_printf("[DEBUG] ZFS txgs interface lost, fallback to basic mode\n");
                }
            }

            last = now;
        }
        struct timespec timeout; clock_gettime(CLOCK_REALTIME, &timeout); timeout.tv_nsec += 20000000;
        if (timeout.tv_nsec >= 1000000000) { timeout.tv_sec++; timeout.tv_nsec -= 1000000000; }
        pthread_mutex_lock(&ring_mtx);
        if (g_ring.count == 0) pthread_cond_timedwait(&ring_cnd, &ring_mtx, &timeout);
        pthread_mutex_unlock(&ring_mtx);
    }
    return NULL;
}

static void *prod_func(void *arg) {
    int fd = *(int *)arg;
    char buf[MAX_EVENTS];
    while (!exit_flag) {
        fd_set rfs; FD_ZERO(&rfs); FD_SET(fd, &rfs);
        struct timeval tv = {1, 0};
        if (select(fd + 1, &rfs, NULL, NULL, &tv) <= 0) continue;
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len <= 0) continue;
        struct fanotify_event_metadata *md = (struct fanotify_event_metadata *)buf;
        while (FAN_EVENT_OK(md, len)) {
            if (md->fd >= 0) {
                if (md->pid == self_pid) { close(md->fd); goto next; }
                if (filter_pid && md->pid != filter_pid) { close(md->fd); goto next; }
                char p[PATH_MAX], fdp[64]; snprintf(fdp, 64, "/proc/self/fd/%d", md->fd);
                if (readlink(fdp, p, PATH_MAX - 1) >= 0) {
                    proc_info_t *pi = get_proc(md->pid);
                    if (!pi) { close(md->fd); goto next; }
                    stats_t *st = &p_stats[pi - p_cache];
                    if (filter_process && strcmp(pi->comm, filter_process) != 0) { close(md->fd); goto next; }
                    history_entry_t ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.pid = md->pid;
                    ev.mask = md->mask;
                    ev.st_ptr = st;
                    clock_gettime(CLOCK_REALTIME, &ev.ts);
                    safe_strncpy(ev.comm_cache, pi->comm, 16);
                    safe_strncpy(ev.parent_comm_cache, pi->parent_comm, 16);
                    ev.ppid_cache = pi->ppid;
                    safe_strncpy(ev.path, p, PATH_MAX);
                    struct stat sb; if (stat(p, &sb) == 0) { ev.inode = sb.st_ino; ev.dev = sb.st_dev; }
                    safe_strncpy(ev.container, st->container, CGROUP_PATH_MAX);

                    pthread_mutex_lock(&cache_mtx);
                    g_shadow.user_pid = md->pid;
                    safe_strncpy(g_shadow.user_comm, pi->comm, sizeof(g_shadow.user_comm));
                    g_shadow.last_mask = md->mask;
                    g_shadow.last_ts = time(NULL);
                    pthread_mutex_unlock(&cache_mtx);

                    pthread_mutex_lock(&ring_mtx);
                    if (g_ring.count < RING_BUFFER_SIZE) {
                        g_ring.buffer[g_ring.tail % RING_BUFFER_SIZE] = ev;
                        g_ring.tail++;
                        g_ring.count++;
                        pthread_cond_signal(&ring_cnd);
                    }
                    pthread_mutex_unlock(&ring_mtx);
                }
                close(md->fd);
            }
        next:
            md = FAN_EVENT_NEXT(md, len);
        }
    }
    return NULL;
}

/* ==================== 报告生成==================== */
typedef struct { int idx; double score; } rank_entry_t;
static int compare_rank_stable(const void *a, const void *b) {
    const rank_entry_t *ra = (const rank_entry_t *)a;
    const rank_entry_t *rb = (const rank_entry_t *)b;
    if (rb->score > ra->score) return 1;
    if (rb->score < ra->score) return -1;
    return 0;
}

static void generate_report(void) {
    time_t now = time(NULL);
    printf_lang("\n%s\n", g_msg->report_title);
    printf_lang("%s: %s\n", g_msg->disks, disk_names);
    printf_lang("%s: %lds\n", "Duration", (long)(now - start_t));

    rank_entry_t ranks[MAX_PID_CACHE];
    int count = 0;
    for (int i = 0; i < p_count; i++) {
        if (p_cache[i].valid && p_stats[i].events > 0) {
            stats_t *st = &p_stats[i];
            double score = st->real_io * 10.0 + st->w_bytes * 0.01 + st->dirty_bytes * 0.001;
            ranks[count].idx = i;
            ranks[count].score = score;
            count++;
        }
    }
    if (count > 1) qsort(ranks, count, sizeof(rank_entry_t), compare_rank_stable);
    printf_lang("%s:\n", g_msg->suspect);
    int top = count < 20 ? count : 20;
    for (int i = 0; i < top; i++) {
        int idx = ranks[i].idx;
        proc_info_t *info = &p_cache[idx];
        stats_t *st = &p_stats[idx];
        char cinfo[CONTAINER_INFO_LEN] = {0};
        if (st->container[0]) {
            if (g_msg == &lang_cn)
                snprintf(cinfo, sizeof(cinfo), "[容器:%s]", st->container);
            else
                snprintf(cinfo, sizeof(cinfo), "[Container:%s]", st->container);
        } else {
            safe_strncpy(cinfo, g_msg->host_label, sizeof(cinfo));
        }
        printf_lang("PID:%-8d [%-15s] <- PPID:%-8d [%-15s] Real:%-5lu Cache:%-5lu Dirty:%-6luMB %s\n",
                    info->pid, info->comm, info->ppid, info->parent_comm,
                    (unsigned long)st->real_io, (unsigned long)st->cache_hit,
                    (unsigned long)(st->dirty_bytes / 1024 / 1024), cinfo);
    }

    printf_lang("\n============================================================\n");
    printf_lang("%s\n", g_msg->diag_header);
    printf_lang("============================================================\n");
    printf_lang("1. [%s]\n", g_msg->env_diag);
    const char *fs_names[] = {"Unknown", "EXT4", "XFS", "ZFS", "BTRFS"};
    printf_lang("   - Filesystem: %s\n", fs_names[g_fs_type]);
    if (g_fs_type == 3 && g_zfs_pool[0]) {
        printf_lang("   - ZFS Pool: %s\n", g_zfs_pool);
        printf_lang("   - TXG Attribution: %s\n", g_zfs_txg_enabled ? "Enabled" : "Disabled (txgs interface not found)");
    }
    printf_lang("   - Mount Options: %s\n", g_has_noatime ? "noatime (Optimal)" : "relatime/atime (Optimizable)");
    printf_lang("   - Disk Type: %s\n", g_is_hdd ? "HDD (Mechanical)" : "SSD/NVMe");
    if (g_has_docker) printf_lang("   - Detected Docker workload\n");
    printf_lang("\n2. %s\n", g_msg->advice_header);
    printf_lang("------------------------------------------------------------\n");
    int advice_cnt = 0;
    if (!g_has_noatime && top > 0 && p_stats[ranks[0].idx].cache_hit > p_stats[ranks[0].idx].real_io) {
        printf_lang("* %s\n", g_msg->advice_atime); advice_cnt++;
    }
    if (g_has_docker && g_is_hdd) {
        printf_lang("* %s\n", g_msg->advice_docker); advice_cnt++;
    }
    if (g_fs_type == 1 && top > 0) {
        for (int i = 0; i < top; i++) {
            int idx = ranks[i].idx;
            if (strncmp(p_cache[idx].comm, "jbd2/", 5) == 0) {
                printf_lang("* %s\n", g_msg->advice_commit); advice_cnt++;
                break;
            }
        }
    }
    if (g_swap_on_disk) {
        printf_lang("* %s\n", g_msg->advice_swap); advice_cnt++;
    }
    for (int i = 0; i < top; i++) {
        int idx = ranks[i].idx;
        if (strstr(p_cache[idx].comm, "rsyslogd") || strstr(p_cache[idx].comm, "syslog") || strstr(p_cache[idx].comm, "journald")) {
            printf_lang("* %s\n", g_msg->advice_log); advice_cnt++;
            break;
        }
    }
    if (advice_cnt == 0) {
        printf_lang("* %s\n", g_msg == &lang_cn ? "未发现明显优化点，系统IO行为正常。" : "No obvious optimization points detected. System IO behavior is normal.");
    }
    printf_lang("============================================================\n");
}

static void write_report_to_csv(FILE *csv) {
    if (!csv) return;
    time_t now = time(NULL);
    fprintf(csv, "\n# === %s ===\n", g_msg->report_title);
    fprintf(csv, "# %s: %s\n", g_msg->disks, disk_names);
    fprintf(csv, "# %s: %lds\n", "Duration", (long)(now - start_t));
    fprintf(csv, "# %s:\n", g_msg->suspect);
    rank_entry_t ranks[MAX_PID_CACHE];
    int count = 0;
    for (int i = 0; i < p_count; i++) {
        if (p_cache[i].valid && p_stats[i].events > 0) {
            stats_t *st = &p_stats[i];
            double score = st->real_io * 10.0 + st->w_bytes * 0.01 + st->dirty_bytes * 0.001;
            ranks[count].idx = i;
            ranks[count].score = score;
            count++;
        }
    }
    if (count > 1) qsort(ranks, count, sizeof(rank_entry_t), compare_rank_stable);
    int top = count < 20 ? count : 20;
    for (int i = 0; i < top; i++) {
        int idx = ranks[i].idx;
        proc_info_t *info = &p_cache[idx];
        stats_t *st = &p_stats[idx];
        char cinfo[CONTAINER_INFO_LEN] = {0};
        if (st->container[0]) {
            if (g_msg == &lang_cn)
                snprintf(cinfo, sizeof(cinfo), "[容器:%s]", st->container);
            else
                snprintf(cinfo, sizeof(cinfo), "[Container:%s]", st->container);
        } else {
            safe_strncpy(cinfo, g_msg->host_label, sizeof(cinfo));
        }
        fprintf(csv, "# PID:%-8d [%-15s] <- PPID:%-8d [%-15s] Real:%-5lu Cache:%-5lu Dirty:%-6luMB %s\n",
                info->pid, info->comm, info->ppid, info->parent_comm,
                (unsigned long)st->real_io, (unsigned long)st->cache_hit,
                (unsigned long)(st->dirty_bytes / 1024 / 1024), cinfo);
    }
    fprintf(csv, "\n# === %s ===\n", g_msg->diag_header);
    fprintf(csv, "# %s:\n", g_msg->env_diag);
    const char *fs_names[] = {"Unknown", "EXT4", "XFS", "ZFS", "BTRFS"};
    fprintf(csv, "# - Filesystem: %s\n", fs_names[g_fs_type]);
    if (g_fs_type == 3 && g_zfs_pool[0]) {
        fprintf(csv, "# - ZFS Pool: %s\n", g_zfs_pool);
        fprintf(csv, "# - TXG Attribution: %s\n", g_zfs_txg_enabled ? "Enabled" : "Disabled");
    }
    fprintf(csv, "# - Mount Options: %s\n", g_has_noatime ? "noatime (Optimal)" : "relatime/atime (Optimizable)");
    fprintf(csv, "# - Disk Type: %s\n", g_is_hdd ? "HDD" : "SSD/NVMe");
    fprintf(csv, "# %s:\n", g_msg->advice_header);
    int advice_cnt = 0;
    if (!g_has_noatime && top > 0 && p_stats[ranks[0].idx].cache_hit > p_stats[ranks[0].idx].real_io) {
        fprintf(csv, "# * %s\n", g_msg->advice_atime); advice_cnt++;
    }
    if (g_has_docker && g_is_hdd) {
        fprintf(csv, "# * %s\n", g_msg->advice_docker); advice_cnt++;
    }
    if (g_fs_type == 1 && top > 0) {
        for (int i = 0; i < top; i++) {
            int idx = ranks[i].idx;
            if (strncmp(p_cache[idx].comm, "jbd2/", 5) == 0) {
                fprintf(csv, "# * %s\n", g_msg->advice_commit); advice_cnt++;
                break;
            }
        }
    }
    if (g_swap_on_disk) {
        fprintf(csv, "# * %s\n", g_msg->advice_swap); advice_cnt++;
    }
    for (int i = 0; i < top; i++) {
        int idx = ranks[i].idx;
        if (strstr(p_cache[idx].comm, "rsyslogd") || strstr(p_cache[idx].comm, "syslog") || strstr(p_cache[idx].comm, "journald")) {
            fprintf(csv, "# * %s\n", g_msg->advice_log); advice_cnt++;
            break;
        }
    }
    if (advice_cnt == 0) {
        fprintf(csv, "# * %s\n", g_msg == &lang_cn ? "未发现明显优化点，系统IO行为正常。" : "No obvious optimization points detected. System IO behavior is normal.");
    }
}

/* ==================== 辅助函数==================== */
static time_t parse_time_or_offset(const char *str) {
    if (!str) return -1;
    if (str[0] == '+') {
        char unit; int val;
        if (sscanf(str + 1, "%d%c", &val, &unit) != 2) return -1;
        time_t offset = 0;
        switch(unit) {
            case 'm': offset = val * 60; break;
            case 'h': offset = val * 3600; break;
            case 'd': offset = val * 86400; break;
            default: return -1;
        }
        return time(NULL) + offset;
    } else {
        int y, m, d, h, min;
        if (sscanf(str, "%4d%02d%02d%02d%02d", &y, &m, &d, &h, &min) != 5) return -1;
        struct tm tm = {0}; tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = min; tm.tm_sec = 0;
        return mktime(&tm);
    }
}

static time_t parse_duration(const char *str) {
    if (!str) return 0;
    char unit; int val;
    if (sscanf(str, "%d%c", &val, &unit) != 2) return 0;
    switch(unit) {
        case 'm': return val * 60;
        case 'h': return val * 3600;
        case 'd': return val * 86400;
        default: return 0;
    }
}

static void roll_log_file(void) {
    if (!csv_stream || roll_interval == 0) return;
    time_t now = time(NULL);
    if (now - last_roll_time < roll_interval) return;
    fflush(csv_stream); fclose(csv_stream);
    char new_path[PATH_MAX]; struct tm tm; localtime_r(&now, &tm);
    snprintf(new_path, sizeof(new_path), "%s_%04d%02d%02d_%02d%02d%02d.csv",
             log_file ? log_file : "iowatch", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    csv_stream = fopen(new_path, "w");
    if (csv_stream) {
        fprintf(csv_stream, "Timestamp,Action,PID,Process,PPID,ParentProcess,Path,IsReal,Container,Inode\n");
        printf_lang("%s\n", g_msg->log_rotated, new_path);
        safe_strncpy(current_log_file, new_path, PATH_MAX - 1);
        last_roll_time = now;
    }
}

static int detect_self_io(const char *log_path, const char *mount) {
    if (!log_path || !mount) return 0;
    struct stat st_log, st_mount;
    if (stat(log_path, &st_log) != 0) {
        char dir[PATH_MAX]; safe_strncpy(dir, log_path, sizeof(dir));
        char *last = strrchr(dir, '/');
        if (last) { *last = '\0'; if (stat(dir, &st_log) == 0 && stat(mount, &st_mount) == 0) return (st_log.st_dev == st_mount.st_dev); }
        return 0;
    }
    if (stat(mount, &st_mount) != 0) return 0;
    return (st_log.st_dev == st_mount.st_dev);
}

static void print_help(const char *name) {
    printf(g_msg->help_text, name, name, name, name, name);
}

static void sig_h(int s) { (void)s; exit_flag = 1; }

/* ==================== 主函数==================== */
int main(int argc, char *argv[]) {
    int opt; self_pid = getpid();
    static struct option long_opts[] = {
        {"output", required_argument, 0, 'o'},
        {"read", no_argument, 0, 'R'},
        {"pid", required_argument, 0, 'p'},
        {"process", required_argument, 0, 'P'},
        {"daemon", no_argument, 0, 'D'},
        {"start", required_argument, 0, 's'},
        {"duration", required_argument, 0, 'e'},
        {"roll", required_argument, 0, 'r'},
        {"quiet", no_argument, 0, 'q'},
        {"lang", required_argument, 0, 'L'},
        {"test", no_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "o:Rp:P:Ds:e:r:qL:tvdh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'L':
                if (strcmp(optarg, "cn") == 0) g_msg = &lang_cn;
                else g_msg = &lang_en;
                break;
            case 'h': print_help(argv[0]); return 0;
            case 'o': log_file = optarg; break;
            case 'R': monitor_read = 1; break;
            case 'p': filter_pid = atoi(optarg); break;
            case 'P': filter_process = optarg; break;
            case 'D': daemon_mode = 1; break;
            case 's': start_time_str = optarg; break;
            case 'e': duration_str = optarg; break;
            case 'r': roll_interval_str = optarg; break;
            case 'q': quiet_mode = 1; break;
            case 't': test_mode = 1; break;
            case 'd': debug_mode = 1; break;
            case 'v': debug_mode = 1; break;
            default: print_help(argv[0]); return 1;
        }
    }

    if (optind >= argc) { fprintf(stderr, "Error: missing mount point\n"); return 1; }
    char *mnt = argv[optind];

    if (daemon_mode && log_file == NULL) { fprintf(stderr, "Error: Daemon mode (-D) requires log file (-o)\n"); return 1; }
    if (quiet_mode && !daemon_mode) printf_lang("%s\n", g_msg->quiet_mode);
    if (geteuid() != 0) { fprintf(stderr, "Error: Need root privileges\n"); return 1; }
    struct utsname sysinfo; uname(&sysinfo);
    if (strcmp(sysinfo.sysname, "Linux") != 0) { fprintf(stderr, "Error: Linux only\n"); return 1; }

    time_t now = time(NULL);
    if (start_time_str) {
        scheduled_start = parse_time_or_offset(start_time_str);
        if (scheduled_start == -1 || scheduled_start <= now) { fprintf(stderr, "Error: Invalid start time\n"); return 1; }
    } else { scheduled_start = now; }

    if (duration_str) {
        run_duration = parse_duration(duration_str);
        if (run_duration <= 0) { fprintf(stderr, "Error: Invalid duration\n"); return 1; }
    }
    if (roll_interval_str) {
        roll_interval = parse_duration(roll_interval_str);
        if (roll_interval <= 0) { fprintf(stderr, "Error: Invalid roll interval\n"); return 1; }
    }

    for (int i = 0; i < MAX_PID_CACHE; i++) p_hash[i] = -1;
    init_sys(mnt);
    env_probe(mnt);
    if (g_disk_cnt == 0 && !test_mode) { fprintf(stderr, "Error: no disk found\n"); return 1; }
    if (log_file && detect_self_io(log_file, mnt)) { fprintf(stderr, "Error: Log file on monitored disk\n"); return 1; }

    if (log_file) {
        char dir[PATH_MAX]; safe_strncpy(dir, log_file, sizeof(dir));
        char *last = strrchr(dir, '/');
        if (last) { *last = '\0'; mkdir(dir, 0755); }
    }
    if (test_mode) {
        printf("Test mode: Disk detection successful\nFound %d disks: %s\n", g_disk_cnt, disk_names);
        return 0;
    }

    fan_fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY | O_LARGEFILE);
    if (fan_fd < 0) { perror("fanotify_init"); return 1; }
    printf_lang("Fanotify initialized (notify-only mode)\n");
    uint64_t mask = FAN_OPEN | FAN_MODIFY | FAN_CLOSE_WRITE | FAN_EVENT_ON_CHILD;
    if (monitor_read) mask |= FAN_ACCESS;
    if (fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM, mask, AT_FDCWD, mnt) < 0) {
        perror("fanotify_mark"); close(fan_fd); return 1;
    }

    if (access("/sys/kernel/debug/tracing/events/block/block_rq_issue/enable", W_OK) == 0) {
        FILE *f = fopen("/sys/kernel/debug/tracing/events/block/block_rq_issue/enable", "w");
        if (f) { fprintf(f, "1"); fclose(f); }
        trace_fd = open_best_trace_pipe();
        if (trace_fd >= 0) {
            printf_lang("✅ block_rq_issue trace enabled\n");
            pthread_create(&trace_th, NULL, trace_reader_thread, NULL);
            pthread_create(&block_th, NULL, block_consumer_thread, NULL);
        }
    }

    signal(SIGINT, sig_h);
    signal(SIGTERM, sig_h);
    if (log_file) {
        csv_stream = fopen(log_file, "w");
        if (csv_stream) {
            fprintf(csv_stream, "Timestamp,Action,PID,Process,PPID,ParentProcess,Path,IsReal,Container,Inode\n");
            safe_strncpy(current_log_file, log_file, PATH_MAX - 1);
            last_roll_time = time(NULL);
        } else { fprintf(stderr, "Error: Cannot create log file\n"); close(fan_fd); return 1; }
    }

    read_diskstats();
    start_t = time(NULL);
    idle_t = start_t;
    if (scheduled_start > start_t) {
        printf_lang("%s\n", g_msg->waiting_start);
        while (!exit_flag && time(NULL) < scheduled_start) sleep(1);
        if (exit_flag) { generate_report(); if (csv_stream) fclose(csv_stream); close(fan_fd); return 0; }
        start_t = time(NULL); idle_t = start_t;
    }

    if (daemon_mode) {
        if (daemon(0, 0) < 0) { perror("daemon"); return 1; }
        int nf = open("/dev/null", O_RDWR);
        if (nf >= 0) { dup2(nf, STDOUT_FILENO); dup2(nf, STDERR_FILENO); close(nf); }
        printf_lang("%s: %s\n", g_msg->daemon_mode, g_msg->mon_start);
    }

    if (!quiet_mode && !daemon_mode) {
        printf_lang("%s\n", g_msg->title);
        printf_lang("%s: %s | %s: %s\n", g_msg->disks, disk_names, g_msg->mon_start, ctime(&start_t));
        if (run_duration > 0) printf_lang("Duration: %lds\n", (long)run_duration);
        printf_lang(g_msg->header_title, "TIME", "ACTION", "PID", "PROCESS", "PPID", "P-PROCESS", "TYPE", "FILE", "CONTAINER");
        printf_lang("------------------------------------------------------------------------------------------------------------------------\n");
    }

    pthread_create(&prod, NULL, prod_func, &fan_fd);
    pthread_create(&cons, NULL, cons_func, NULL);
    while (!exit_flag) {
        sleep(1);
        if (run_duration > 0 && (time(NULL) - start_t) >= run_duration) {
            printf_lang("%s\n", g_msg->duration_expired);
            exit_flag = 1;
        }
    }

    pthread_cond_broadcast(&ring_cnd);
    pthread_cond_broadcast(&block_cnd);
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);
    if (trace_fd >= 0) {
        pthread_join(trace_th, NULL);
        pthread_join(block_th, NULL);
        close(trace_fd);
        FILE *f = fopen("/sys/kernel/debug/tracing/events/block/block_rq_issue/enable", "w");
        if (f) { fprintf(f, "0"); fclose(f); }
    }
    close(fan_fd);
    generate_report();
    if (csv_stream) { write_report_to_csv(csv_stream); fclose(csv_stream); }
    return 0;
}