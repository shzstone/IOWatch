/*
 * IOWATCH - 磁盘休眠分析工具 (ARM/x86_64)
 * 编译：gcc -O3 -static iowatch.c -o iowatch
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/fanotify.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <libgen.h>
#include <sys/utsname.h>

void print_usage(char *prog_name) {
    printf("\n================================================================\n");
    printf(" IOWATCH - 磁盘休眠分析工具 (ARM/x86_64)                       \n");
    printf("================================================================\n");
    printf("核心规则：\n");
    printf("  监控目标必须是一个【挂载点】根目录（如 / 或 /volume1）。\n");
    printf("  请通过 'df -h' 命令查看 \"Mounted on\" 一栏确认路径。\n\n");
    printf("用法：\n");
    printf("  sudo %s <挂载点路径>\n", prog_name);
    printf("示例：\n");
    printf("  sudo %s /volume1\n", prog_name);
    printf("================================================================\n\n");
}

int check_kernel_version(void) {
    struct utsname buf;
    if (uname(&buf) < 0) return 0;
    int major, minor;
    if (sscanf(buf.release, "%d.%d", &major, &minor) != 2) return 0;
    return (major > 5 || (major == 5 && minor >= 1));
}

const char* get_action(uint64_t mask) {
    if (mask & FAN_MODIFY) return "MODIFY";
    if (mask & FAN_CLOSE_WRITE) return "WRITE_CLOSE";
    if (mask & FAN_ACCESS) return "READ";
    if (mask & FAN_OPEN) return "OPEN";
    return "EVENT";
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    struct utsname sys_info;
    if (uname(&sys_info) < 0 || strcmp(sys_info.sysname, "Linux") != 0) {
        fprintf(stderr, "[错误] 本程序仅支持 Linux 系统。\n");
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "[错误] 需要 root 权限。请使用 sudo 运行。\n");
        return 1;
    }

    if (!check_kernel_version()) {
        fprintf(stderr, "[错误] 内核版本过低，需要 Linux 5.1 或更高版本。\n");
        fprintf(stderr, "        当前内核版本: %s\n", sys_info.release);
        return 1;
    }

    int fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY);
    if (fd < 0) {
        if (errno == EPERM) {
            fprintf(stderr, "[错误] 缺少 CAP_SYS_ADMIN 权限，请使用 root 运行。\n");
        } else if (errno == ENOSYS) {
            fprintf(stderr, "[错误] 当前内核未编译 fanotify 支持。\n");
        } else {
            perror("fanotify_init 失败");
        }
        return 1;
    }

    uint64_t mask = FAN_MODIFY | FAN_CLOSE_WRITE | FAN_ACCESS | FAN_OPEN | FAN_EVENT_ON_CHILD;
    if (fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM, mask, AT_FDCWD, argv[1]) < 0) {
        fprintf(stderr, "[错误] 无法监控目标路径: %s\n", argv[1]);
        if (errno == ENOENT) {
            fprintf(stderr, "提示：请运行 'df -h' 确保该路径是基础挂载点。\n");
        } else {
            perror("原因");
        }
        return 1;
    }

    print_usage(argv[0]);
    printf("%-20s | %-12s | %-7s | %-15s | %s\n",
           "时间戳", "动作", "PID", "进程名", "文件路径 [触发程序]");
    printf("----------------------------------------------------------------------------------------------------\n");

    char buf[8192];
    while (1) {
        ssize_t len = read(fd, buf, sizeof(buf));
        if (len < 0) break;

        struct fanotify_event_metadata *metadata = (struct fanotify_event_metadata *)buf;
        while (FAN_EVENT_OK(metadata, len)) {
            if (metadata->fd >= 0) {
                char f_path[PATH_MAX], e_path[PATH_MAX], ts[20], proc_comm[32];

                sprintf(f_path, "/proc/self/fd/%d", metadata->fd);
                ssize_t p_len = readlink(f_path, f_path, sizeof(f_path)-1);
                if (p_len != -1) f_path[p_len] = '\0';
                else strcpy(f_path, "未知路径");

                sprintf(proc_comm, "/proc/%d/comm", metadata->pid);
                int c_fd = open(proc_comm, O_RDONLY);
                if (c_fd >= 0) {
                    ssize_t c_len = read(c_fd, proc_comm, 31);
                    if (c_len > 0) proc_comm[c_len-1] = '\0';
                    close(c_fd);
                } else strcpy(proc_comm, "已退出");

                char raw_exe[PATH_MAX];
                sprintf(raw_exe, "/proc/%d/exe", metadata->pid);
                ssize_t e_len = readlink(raw_exe, raw_exe, sizeof(raw_exe)-1);
                if (e_len != -1) {
                    raw_exe[e_len] = '\0';
                    strcpy(e_path, basename(raw_exe));
                } else {
                    strcpy(e_path, "未知");
                }

                time_t now = time(NULL);
                strftime(ts, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));

                if (metadata->pid != getpid()) {
                    printf("%-20s | %-12s | %-7d | %-15s | %s [%s]\n",
                           ts, get_action(metadata->mask), metadata->pid, proc_comm, f_path, e_path);
                }
                close(metadata->fd);
            }
            metadata = FAN_EVENT_NEXT(metadata, len);
        }
    }
    return 0;
}
