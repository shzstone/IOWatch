# IOWatch - Disk Hibernation Analysis Tool

English | [简体中文](./README.md)

---

## 🇺🇸 English

**IOWatch** is a lightweight, high-performance disk I/O monitoring tool specifically designed to analyze the "culprits" preventing **hard drive hibernation** on Linux systems (such as NAS and OpenWrt routers).

### ✨ Key Features
* **Precise Pinpointing**: Leverages the `fanotify` mechanism to not only see which files are being read/written but also trace the specific **program path** that triggered the I/O.
* **Docker Transparency**: Even if processes are running inside Docker containers, it accurately displays their executable paths on the host system.
* **Extremely Low Overhead**: Based on kernel event-driven architecture with no polling, ensuring minimal CPU and memory usage—perfect for long-term monitoring on embedded devices.
* **Zero Dependencies**: Statically compiled and independent of GLIBC. Just copy and run on any Linux distribution.

### 📥 Quick Start (No Compilation Required)
1. **Download**: Download the binary for your architecture from the **[Releases](https://github.com/guoshh1978/IOWatch/releases)** page (ARM64 for mainstream NAS, x86_64 for regular PCs/servers).
2. **Upload**: Upload the file to your device via SCP, cloud drive, or Synology File Station.
3. **Run**:
```bash
chmod +x iowatch-arm64
df -h  # Check the mount point, e.g., /volume1 or /mnt/sda1
sudo ./iowatch-arm64 /volume1
```

### ⚙️ Requirements
* **Kernel Version**: Linux 5.1+ is recommended.
* **Privileges**: Must be run as `root` or with `sudo`.
* **Legacy Kernel Adaptation**: If it fails on older kernels, please replace `FAN_MARK_FILESYSTEM` with `FAN_MARK_MOUNT` in the source code and compile manually.

[Back to Top](#iowatch---disk-hibernation-analysis-tool)
