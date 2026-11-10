# FirstOS 🖥️🌐

A modern, high-performance, 64-bit operating system built from scratch in C and Assembly, featuring Symmetric Multiprocessing (SMP), a hardware-accelerated True Color window compositor, a native TCP/IP networking stack, Intel HDA sound, and an integrated native HTML5 web browser.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#building-firstos)
[![Architecture](https://img.shields.io/badge/arch-x86__64-blue.svg)](#system-architecture)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](#license)

---

## 🌟 Key Features

### ⚡ 64-bit SMP Kernel & Isolation
* **64-bit Long Mode**: Native `x86_64` kernel with multi-level paging (PML4) and strict ring-0 / ring-3 privilege separation.
* **Symmetric Multiprocessing (SMP)**: Multi-core initialization, inter-processor interrupts (IPI), and core scheduling.
* **Process & Memory Management**: Virtual memory manager, kernel heap allocator, copy-on-write page isolation, POSIX syscalls, threads (`pthreads`), futexes, and IPC pipes.
* **Crash Supervisor & Process Isolation**: Self-healing process supervisor (`app_supervisor`) preventing rogue user applications from compromising kernel stability.

### 🎨 Accelerated Graphics & Modern Desktop GUI
* **VirtIO GPU & Framebuffer**: Hardware-accelerated 2D pipeline via VirtIO-GPU PCI with software double-buffering fallback.
* **Window Compositor**: True Color (32-bit ARGB) compositing engine with dirty-rectangle damage tracking, alpha blending, and occlusion culling.
* **macOS-Inspired Desktop Environment**:
  * Smooth draggable, resizable windows with snapping and z-order arbitration.
  * System menu bar, real-time battery/clock/weather telemetry.
  * Interactive application Dock with hover states.
  * Spotlight quick search bar.
  * Wallpaper support (PNG, JPG, BMP, TIFF) and custom system themes.

### 🌐 Native Web Browser Engine
* **Pure C HTML5 Parser**: Custom DOM tree construction and tokenizer.
* **CSS & Text Layout Engine**: Block/inline flow, flexbox layouts, text wrapping, and viewport scrolling.
* **Single-Page Application (SPA) Synthesizer**: In-engine payload extraction for modern dynamic web platforms (YouTube, Google, Wikipedia, GitHub).
* **Browser Features**: Multi-tab browsing, quick-access bookmarks, cookie storage, persistent cache, and download manager.

### 🔌 Full TCP/IP Networking Stack
* **Hardware Drivers**: VirtIO Network (`virtio-net-pci`) and Intel E1000 Gigabit Ethernet.
* **Complete Protocol Suite**: Ethernet, ARP, IPv4, ICMP (ping), UDP, DHCP, DNS client, and stateful TCP with sliding-window flow control.
* **HTTP/1.1 & Proxy Bridge**: Native HTTP/1.1 client with keep-alive connection pooling, TLS proxy acceleration bridge, and socket abstraction layer.

### 🔊 Audio & Multimedia
* **Intel High Definition Audio (Intel HDA)**: Low-latency multichannel PCM playback and audio mixing.
* **Multimedia Player**: Playback for WAV and MP3 audio files, alongside AVI video rendering.

### 🛠️ Built-in Applications
* 🌐 **Web Browser** (`WebBrowser.app`): Full web browsing with tabs and bookmark bar.
* 💻 **Terminal**: Interactive shell with command history, POSIX utilities (`ls`, `ps`, `df`, `free`, `cat`, etc.).
* 📁 **File Explorer**: Graphical directory navigation, file creation, and execution.
* 📈 **Task Manager**: Real-time CPU, memory, and per-process telemetry.
* 📝 **Code IDE & FirstLang**: In-system application development environment and syntax highlighting editor.
* 🖼️ **Photo Viewer & Editor**: Image viewing and filter transformations.
* 🧮 **Calculator & Widgets**: Desktop productivity tools.

---

## 🏛️ System Architecture

```
+--------------------------------------------------------------------------+
|                            USERSPACE (Ring 3)                            |
|  [ Web Browser ]  [ Terminal ]  [ File Explorer ]  [ Task Manager ]      |
|  [ Media Player]  [ Code IDE ]  [ User Apps (ELF) ] [ C Runtime / Libc ] |
+--------------------------------------------------------------------------+
                                    |  POSIX Syscalls (sysenter/syscall)
+--------------------------------------------------------------------------+
|                            COMPOSITOR & GUI                              |
|  [ Window Manager ] [ 32-bit Compositor ] [ Damage Tracker ] [ Dock/Bar ]|
+--------------------------------------------------------------------------+
                                    |
+--------------------------------------------------------------------------+
|                             KERNEL (Ring 0)                              |
|  +--------------------+  +---------------------+  +--------------------+ |
|  |   SMP & Scheduler  |  | Virtual Memory (Paging) | Process Supervisor| |
|  +--------------------+  +---------------------+  +--------------------+ |
|  |   TCP/IP Stack     |  | VFS & VirtIO Block  |  | Intel HDA Sound    | |
|  +--------------------+  +---------------------+  +--------------------+ |
|  | VirtIO GPU Driver  |  | VirtIO Net / E1000  |  | PCI & APIC / IDT   | |
|  +--------------------+  +---------------------+  +--------------------+ |
+--------------------------------------------------------------------------+
                                    |
+--------------------------------------------------------------------------+
|                          HARDWARE / EMULATION                            |
|             x86_64 CPU (Multi-core) | 2GB+ RAM | VirtIO Devices          |
+--------------------------------------------------------------------------+
```

---

## 🚀 Getting Started

### Prerequisites

You will need an `x86_64-elf` cross-compiler toolchain, NASM, and QEMU.

#### macOS (via Homebrew)
```bash
brew install x86_64-elf-gcc x86_64-elf-binutils nasm qemu
```

#### Ubuntu / Debian Linux
```bash
sudo apt update
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin grub-common xorriso
# For cross-compilation on Linux, install gcc-x86-64-elf or use host gcc with target flags.
```

---

### 🔨 Building FirstOS

1. **Clone the repository:**
   ```bash
   git clone https://github.com/mohamadjlo/firstos.git
   cd firstos
   ```

2. **Compile the kernel, userspace apps, and generate the bootable ISO:**
   ```bash
   make
   ```
   This will:
   - Compile all kernel assembly and C subsystems.
   - Build external userland binaries and standard C library.
   - Construct the `firstos.iso` bootable CD-ROM image with GRUB bootloader.
   - Assemble the virtual disk filesystem image (`fs.img`).

---

### 🖥️ Running in QEMU

Run the standard launch target which starts the system with VirtIO GPU, VirtIO Net, Intel HDA sound, and the network bridge:

```bash
make run
```

Or run directly with software framebuffer fallback:
```bash
make run-fb
```

#### QEMU Launch Parameters (under the hood)
FirstOS is configured to boot in QEMU with:
* `2 GB` RAM
* `xres=1024, yres=768` 32-bit VirtIO GPU
* Intel HDA audio (`intel-hda`, `hda-output`)
* VirtIO Network (`virtio-net-pci` with user-mode network stack)
* VirtIO Block storage (`virtio-blk-pci` for persistent filesystem)
* USB Tablet device for precise pointer coordination

---

## 🌐 Web Browser & Networking Acceleration

FirstOS features a built-in HTTP proxy accelerator bridge (`tools/firstos_webproxy.py`) used in development and emulation to handle host-level HTTPS acceleration and DNS routing transparently.

When launching via `make run`, the script automatically starts the local proxy bridge on port `18080`. The browser can also operate directly over native TCP/IP sockets when deployed on physical or bridged network hardware.

To run the web proxy manually:
```bash
python3 tools/firstos_webproxy.py --bind 127.0.0.1 --port 18080
```

---

## 🧪 Testing & Automated Gates

The repository includes a comprehensive regression testing suite for continuous verification:

```bash
# Run graphics damage and arbitration gates
make test-gfx-gate
make test-gfx-buffer-gate
make test-gfx-async-gate

# Run automated browser and network verification
python3 tools/test_qemu_browser_live.py
python3 tools/test_qemu_youtube_native.py
```

---

## 📁 Repository Structure

```
FirstOS/
├── boot.asm                 # Multiboot-compliant 64-bit bootloader entry
├── cpu_smp.c, cpu_smp.h     # Multi-core Symmetric Multiprocessing (SMP)
├── idt.c, isr.asm           # Interrupt Descriptor Table & ISR handling
├── paging.c, memory.c       # 4-level paging and physical memory management
├── syscall.c, process.c     # POSIX syscalls, process scheduler, Ring 3 isolation
├── compositor.c, gui.c      # Window manager, 32-bit compositing engine, dock
├── virtio_gpu.c, fb.c       # VirtIO-GPU and 2D hardware graphics pipeline
├── virtio_net.c, eth.c      # Network drivers and Ethernet frame processing
├── tcp.c, udp.c, ip.c       # Native TCP/IP protocol implementations
├── http_client.c, http_v11.c# HTTP/1.1 networking client and parser
├── web_browser_app.c        # FirstOS Web Browser UI application
├── html5_engine.c           # HTML5 tokenizer and DOM tree generator
├── hda.c, sound.c           # Intel High Definition Audio driver
├── terminal.c, explorer.c   # Desktop applications (Terminal, File Explorer)
├── apps/                    # Ring 3 userspace applications
├── tools/                   # Testing harnesses, build helpers, proxy bridge
└── Makefile                 # Master build and execution rules
```

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
