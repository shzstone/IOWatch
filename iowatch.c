/*
 * IOWATCH - 磁盘休眠分析工具
 * 功能：监控指定挂载点的所有文件访问事件
 * 编译：gcc -O3 -static iowatch_clean.c -o iowatch
 * 作者：Stone
 * 官方博客：https://blog.cacca.cc
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

void print_usage(char *prog_name) {
    printf("\n================================================================\n");
    printf(" IOWATCH - 磁盘休眠分析工具 (ARM/x86_64)                       \n");
    printf("================================================================\n");
    printf("核心规则：\n");
    printf("  监控目标必须是一个【挂载点】根目录（如 / 或 /volume1）。\n");
    printf("  请通过 'df -h' 命令查看 \"Mounted on\" 一栏确认路径。\n\n");
    printf("用法：\n");
    printf("  sudo {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s <挂载点路径>\n", prog_name);
    printf("示例：\n");
    printf("  sudo {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s /volume1\n", prog_name);
    printf("================================================================\n\n");
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

    // 初始化 fanotify 句柄
    int fd = fanotify_init(FAN_CLASS_NOTIF, O_RDONLY);
    if (fd < 0) {
        if (errno == EPERM) fprintf(stderr, "[错误] 需要 root 权限。\n");
        else perror("fanotify_init 失败");
        return 1;
    }

    // 设置监控掩码：修改、写入关闭、读取、打开以及子目录事件
    uint64_t mask = FAN_MODIFY | FAN_CLOSE_WRITE | FAN_ACCESS | FAN_OPEN | FAN_EVENT_ON_CHILD;
    
    // 标记监控目标：FAN_MARK_FILESYSTEM 支持整个文件系统监控 (内核 5.1+)
    if (fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM, mask, AT_FDCWD, argv[1]) < 0) {
        fprintf(stderr, "[错误] 无法监控目标路径: {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s\n", argv[1]);
        if (errno == ENOENT) {
            fprintf(stderr, "提示：请运行 'df -h' 确保该路径是基础挂载点。\n");
        } else {
            perror("原因");
        }
        return 1;
    }

    print_usage(argv[0]);
    printf("{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-20s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-12s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-7s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-15s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s\n", 
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

                // 1. 获取受影响的文件路径
                sprintf(f_path, "/proc/self/fd/{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}d", metadata->fd);
                ssize_t p_len = readlink(f_path, f_path, sizeof(f_path)-1);
                if (p_len != -1) f_path[p_len] = '\0';
                else strcpy(f_path, "未知路径");

                // 2. 获取触发进程的简称 (Comm)
                sprintf(proc_comm, "/proc/{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}d/comm", metadata->pid);
                int c_fd = open(proc_comm, O_RDONLY);
                if (c_fd >= 0) {
                    ssize_t c_len = read(c_fd, proc_comm, 31);
                    if (c_len > 0) proc_comm[c_len-1] = '\0';
                    close(c_fd);
                } else strcpy(proc_comm, "已退出");
                
                // 3. 获取触发程序的执行文件名 (basename)
                char raw_exe[PATH_MAX];
                sprintf(raw_exe, "/proc/{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}d/exe", metadata->pid);
                ssize_t e_len = readlink(raw_exe, raw_exe, sizeof(raw_exe)-1);
                if (e_len != -1) {
                    raw_exe[e_len] = '\0';
                    strcpy(e_path, basename(raw_exe)); 
                } else {
                    strcpy(e_path, "未知");
                }

                // 4. 生成时间戳
                time_t now = time(NULL);
                strftime(ts, 20, "{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}Y-{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}m-{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}d {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}H:{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}M:{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}S", localtime(&now));

                // 排除工具自身的事件
                if (metadata->pid != getpid()) {
                    printf("{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-20s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-12s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-7d | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}-15s | {ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s [{ccea3ec6a8a8e821dbd7d2e3656d35a319e4836b203a189937f98d45dc6a050c}s]\n", 
                           ts, get_action(metadata->mask), metadata->pid, proc_comm, f_path, e_path);
                }
                close(metadata->fd);
            }
            metadata = FAN_EVENT_NEXT(metadata, len);
        }
    }
    return 0;
}
