# IOWATCH - 磁盘休眠分析工具 (ARM/x86_64) 🚀

[![Language](https://img.shields.io/badge/Language-C-blue.svg)](https://en.cppreference.com/w/c)
[![Platform](https://img.shields.io/badge/Platform-Linux-orange.svg)](https://www.kernel.org/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)

**IOWATCH** 是一个轻量级、高性能的磁盘 IO 监控工具，专门用于分析 Linux 系统中阻碍磁盘休眠（HDD Hibernation）的“元凶”。

不同于普通的 `iotop` 或 `lsof`，IOWATCH 利用内核 **fanotify** API，能够实时捕获指定挂载点上所有文件的 **打开(OPEN)**、**读取(READ)**、**修改(MODIFY)** 及 **关闭(CLOSE)** 事件，并精确关联到触发这些事件的**进程 PID** 及 **程序路径**。

---

## ✨ 核心特性

- **文件系统级监控**：支持 `FAN_MARK_FILESYSTEM`，只需指定挂载点，即可监控该分区下的所有子目录和文件。
- **深层追踪**：不仅显示进程名，还能追踪到触发 IO 的具体可执行程序文件路径。
- **极低开销**：基于内核事件驱动机制，对系统性能影响微乎其微，适合在 NAS (群晖/威联通)、iStoreOS、树莓派等嵌入式设备上长期运行。
- **零依赖性**：支持静态编译，生成独立二进制文件，拷贝即用，无需安装任何库。
- **实时时间戳**：精确记录每次访问的时间，方便与系统日志进行对齐分析。

---

## ⚙️ 环境要求

- **操作系统**：Linux 
- **内核版本**：建议 5.1 或更高版本（支持 `FAN_MARK_FILESYSTEM` 标志）
- **权限**：必须使用 `root` 权限运行

---

## 🛠️ 编译与安装

项目仅由一个单体 C 文件组成。建议使用静态编译以获得最大兼容性：

```bash
# 编译
gcc -O3 -static iowatch.c -o iowatch

# 如果在精简版 Linux (如 iStoreOS) 上，可能需要先安装 gcc 
# opkg update && opkg install gcc
```

---

## 🚀 使用说明

### 1. 查找挂载点
首先确认您要监控的磁盘分区挂载在哪里：
```bash
df -h
```
假设您的数据盘挂载在 `/volume1` 或 `/mnt/sda1`。

### 2. 启动监控
```bash
sudo ./iowatch /volume1
```

### 3. 输出示例解析
一旦有程序访问磁盘，终端会实时输出以下格式的日志：

| 时间戳 | 动作 | PID | 进程名 | 文件路径 [触发程序] |
| :--- | :--- | :--- | :--- | :--- |
| 2024-04-01 10:12:05 | READ | 1245 | python3 | /volume1/data/db.sqlite [/usr/bin/python3.10] |
| 2024-04-01 10:12:08 | MODIFY | 890 | smbd | /volume1/share/test.txt [/usr/sbin/smbd] |

- **READ/OPEN**: 代表程序正在读取数据。
- **MODIFY/WRITE_CLOSE**: 代表程序正在写入数据，这是阻碍休眠的最主要原因。

---

## 💡 分析技巧：为什么我的磁盘不休眠？

1. **日志文件**：检查是否有进程在不停地向磁盘写入 `/log` 或 `/tmp` 文件。
2. **数据库扫描**：某些媒体服务器（如 Plex/Emby/Jellyfin）会定期扫描库文件，触发大量 `READ` 事件。
3. **系统服务**：注意像 `smartd`、`samba` 或云盘同步服务是否在后台频繁执行。
4. **定位元凶**：通过输出最后的 `[触发程序]` 路径，你可以清晰地知道是哪个软件包在捣鬼。

---

## 📄 许可证
MIT License

---
**Developed by Stone** | 专注于极致性能与实用的运维利器。
