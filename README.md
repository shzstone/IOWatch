# IOWatch - Disk Hibernation Analysis Tool

[English](./README_en.md) | 简体中文

---

## 🇨🇳 简体中文

**IOWatch** 是一个轻量级、高性能的磁盘 I/O 监控工具，专门用于分析 Linux 系统中（如 NAS、OpenWrt 路由器）阻碍 **硬盘休眠** 的“元凶”。

### ✨ 核心特性
* **精准锁定**：利用 `fanotify` 机制，不仅能看到哪个文件被读写，还能追溯具体是哪个**程序路径**触发的 IO。
* **容器穿透**：即使进程运行在 Docker 容器内部，也能准确显示其在宿主机上的可执行文件路径。
* **极低开销**：基于内核事件驱动，无需轮询，对 CPU 和内存占用极低，适合嵌入式设备长期挂机。
* **零依赖**：采用静态编译，不依赖 GLIBC，在任何 Linux 发行版上都能拷贝即用。

### 📥 快速开始 (无需编译)
1. **下载**：从 **[Releases](https://github.com/guoshh1978/IOWatch/releases)** 页面下载对应架构的二进制文件（ARM64 适用于主流 NAS，x86_64 适用于普通电脑/服务器）。
2. **上传**：通过 SCP、网盘或群晖文件管理器将文件上传到设备。
3. **运行**：
```bash
chmod +x iowatch-arm64
df -h  # 查看挂载点，例如 /volume1 或 /mnt/sda1
sudo ./iowatch-arm64 /volume1
```

### ⚙️ 环境要求
* **内核版本**：建议 Linux 5.1+。
* **权限**：必须以 `root` 或 `sudo` 运行。
* **旧内核适配**：若在旧内核上运行失败，请将源码中 `FAN_MARK_FILESYSTEM` 替换为 `FAN_MARK_MOUNT` 后手动编译。

[返回顶部](#iowatch---disk-hibernation-analysis-tool)
