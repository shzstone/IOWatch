# IOWatch - Physical Disk Sleep & Hibernation Analyzer

<p align="center">
  <a href="#english">English</a> | <a href="#chinese">简体中文</a>
</p>

---

<h2 id="english">🌟 Overview</h2>

**IOWatch** is an advanced, high-performance disk I/O diagnostic tool specifically optimized for Linux-based storage systems (NAS, OpenWrt, Servers). Unlike traditional monitors, IOWatch v1.1 features an **Intelligent Diagnostic Engine** that identifies the root cause of disk wake-ups, even when they are hidden behind kernel threads or filesystem metadata updates.

### 📸 Screenshots

#### 1. Real-time Intelligent Monitoring
Track every file access and see it correlated with physical disk activity in real-time.
![Real-time Monitoring](images/Screen_output.png)

#### 2. Smart Diagnostic Report
Get a summarized report with "Smart Suggestions" to help you optimize your system for disk hibernation.
![Analysis Report](images/report.png)

---

## ✨ Intelligent Features (v1.1)
*   **Shadow Attribution**: Automatically links kernel metadata updates (like `jbd2` or `kworker/atime`) back to the specific user process that triggered them.
*   **ZFS TXG Back-tracing**: Specifically handles ZFS's asynchronous nature by buffering events and attributing them once a Transaction Group (TXG) is committed.
*   **Adaptive Environment Probe**: Automatically detects filesystem types (Ext4, XFS, ZFS, BTRFS), mount options (`noatime`), and disk types (HDD/SSD).
*   **Smart Suggestions**: Provides actionable advice (e.g., "Use log2ram", "Add noatime") based on actual I/O patterns.
*   **Container Penetration**: Native support for Docker/Containerd, identifying workloads even through overlay layers.
*   **Zero Dependency**: Statically compiled, single binary, no external commands (like `lsblk` or `df`) required.

---

## 📥 Quick Start
1.  **Download**: Get the binary for your architecture from the **Releases** page.
2.  **Run**:
    ```bash
    chmod +x iowatch
    # Monitor a mount point (e.g., /volume1)
    sudo ./iowatch /volume1
    ```

---

<h2 id="chinese">🌟 简介</h2>

**IOWatch** 是一款专为 Linux 存储系统（如 NAS、OpenWrt 路由器、服务器）设计的深度磁盘 I/O 诊断工具。v1.1 智能诊断版不仅能监控 IO，还能通过**内核影子归因算法**穿透文件系统层，揪出那些隐藏在内核线程（如 `jbd2`）背后的“休眠杀手”。

### 📸 软件截图

#### 1. 实时智能监控
实时区分 `[真实]` IO 与 `[缓存]` IO，并自动进行元数据归因。
![实时监控](images/Screen_output.png)

#### 2. 智能诊断报告
运行结束自动生成环境评估，并给出针对性的“智能优化建议”。
![分析报告](images/report.png)

---

## ✨ 核心特性 (v1.1)
*   **影子归因 (Shadow Attribution)**：自动将内核元数据更新（如 Atime 更新、日志刷新）关联到具体触发的应用进程。
*   **ZFS 异步追踪**：针对 ZFS 异步写入特性，通过 TXG (事务组) 提交状态进行回溯判定，确保归因准确。
*   **自适应环境探测**：无需外部命令，原生识别文件系统 (Ext4, XFS, ZFS, BTRFS)、挂载参数及磁盘介质类型。
*   **智能优化建议**：根据 IO 模式自动提示优化方案（如：建议开启 `noatime`、使用 `log2ram` 等）。
*   **容器穿透**：完美支持 Docker / Containerd，自动识别容器内部进程并标注。
*   **极致轻量**：纯 C 语言实现，无外部依赖，不调用 `lsblk` 或 `df` 等可能缺失的命令。

---

## 📥 快速开始
1.  **下载**：从 **Releases** 页面下载对应架构的二进制文件。
2.  **运行**：
    ```bash
    chmod +x iowatch
    # 监控挂载点 (例如 /volume1)
    sudo ./iowatch /volume1
    ```

---

## ⚙️ 常用选项
| 选项 | 说明 |
| :--- | :--- |
| `-L cn` | 切换至中文界面 |
| `-o <file>` | 将日志保存为 CSV (**严禁放在被监控的机械硬盘上！**) |
| `-D` | 后台模式，适合长期观测 |
| `-s <time>` | 定时启动 (如 +30m) |
| `-e <dur>` | 自动停止时长 (如 12h) |

---

## ⚠️ 注意事项
*   **权限**：必须以 `root` 或 `sudo` 运行。
*   **内核**：建议 Linux 5.1+ 以获得最佳 `fanotify` 支持。
*   **日志位置**：建议将 CSV 日志保存在 `/tmp` 或 SSD 上，避免由于写入日志本身导致硬盘无法休眠。

---
**IOWatch v1.1** | [GitHub Repository](#)