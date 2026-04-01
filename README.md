# IOWatch - Disk Hibernation Analysis Tool 🚀

[English](#-english) | [简体中文](#-简体中文)

---

## 🇺🇸 English

**IOWatch** is a high-performance disk I/O monitoring utility designed to identify processes that prevent **HDD hibernation (spinning down)** on Linux systems (NAS, OpenWrt, Servers). By leveraging the kernel's **fanotify** API, it provides real-time visibility into every file access at the VFS layer.

### ✨ Key Features
- **Zero Dependencies**: Statically linked binaries—just copy and run on any Linux distro.
- **Filesystem-Wide Monitoring**: Uses `FAN_MARK_FILESYSTEM` to watch an entire drive with one command.
- **Process Correlation**: Accurately maps I/O events to **PID**, **Process Name**, and **Executable Path**.
- **Container Aware**: Traces I/O back to the specific binary even if it's inside a Docker container.
- **Minimal Overhead**: Event-driven; safe for low-resource hardware like ARM routers.

### 📥 Download & Quick Start
1. **Download**: Go to the **[Releases](https://github.com/guoshh1978/IOWatch/releases)** page and download the version for your architecture (`arm64` or `x86_64`).
2. **Upload**: Use SCP or SFTP to upload the binary to your device.
3. **Run**:
```bash
# Give execution permissions
chmod +x iowatch-arm64

# Start monitoring (e.g., monitor your data pool at /volume1)
sudo ./iowatch-arm64 /volume1
```

### 🚀 Output Interpretation
| Timestamp | Action | PID | Process | File Path [Triggering Binary] |
| :--- | :--- | :--- | :--- | :--- |
| 2024-04-01 10:12:05 | READ | 1245 | python3 | /data/db.sqlite [/usr/bin/python3] |
| 2024-04-01 10:12:08 | MODIFY | 890 | smbd | /share/log.txt [/usr/sbin/smbd] |

[Back to Top](#iowatch---disk-hibernation-analysis-tool-)

---

## 🇨🇳 简体中文

**IOWatch** 是一个轻量级、高性能的磁盘 I/O 监控工具，专门用于分析 Linux 系统中（如 NAS、OpenWrt 路由器）阻碍 **硬盘休眠** 的“元凶”。

### ✨ 核心特性
*   **精准锁定**：利用 `fanotify` 机制，不仅能看到哪个文件被读写，还能追溯具体是哪个**程序路径**触发的 IO。
*   **容器穿透**：即使进程运行在 Docker 容器内部，也能准确显示其在宿主机上的可执行文件路径。
*   **极低开销**：基于内核事件驱动，无需轮询，对 CPU 和内存占用极低，适合嵌入式设备长期挂机。
*   **零依赖**：采用静态编译，不依赖 GLIBC，在任何 Linux 发行版上都能拷贝即用。

### 📥 快速开始 (无需编译)
1.  **下载**：从 **[Releases](https://github.com/guoshh1978/IOWatch/releases)** 页面下载对应架构的二进制文件（ARM64 适用于主流 NAS，x86_64 适用于普通电脑/服务器）。
2.  **上传**：通过 SCP、网盘或群晖文件管理器将文件上传到设备。
3.  **运行**：
```bash
chmod +x iowatch-arm64
df -h  # 查看挂载点，例如 /volume1 或 /mnt/sda1
sudo ./iowatch-arm64 /volume1
```

### ⚙️ 环境要求
- **内核版本**：建议 Linux 5.1+。
- **权限**：必须以 `root` 或 `sudo` 运行。
- **旧内核适配**：若在旧内核上运行失败，请将源码中 `FAN_MARK_FILESYSTEM` 替换为 `FAN_MARK_MOUNT` 后手动编译。

[返回顶部](#iowatch---disk-hibernation-analysis-tool-)

---

## 📄 License
MIT License.

---
**Developed by Stone** | *Architect-grade stability for Linux systems.*
