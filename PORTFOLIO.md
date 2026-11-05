# SubhashishOS — Operating System Engineering Case Study

**Author:** Subhashish Reddy  
**Profile:** Incoming MBA Candidate at IIM Shillong | Systems & Product Enthusiast  
**Core Stack:** C, x86_64 Assembly, Bare-Metal Bootstrapping, QEMU, LLVM/Clang

---

# Overview

SubhashishOS is a custom 64-bit operating system developed from scratch to explore low-level systems engineering, memory management, hardware interaction, and graphical interface design.

The project was built to understand how modern operating systems manage:
- hardware resources
- process execution
- memory allocation
- graphical rendering
- file handling
- user interaction

Instead of relying on existing operating systems or frameworks, the project implements core functionalities directly at the hardware and kernel level.

---

# Key Features

## 1. Graphical Window System & 4-App Desktop
- Developed a 64-bit QWORD double-buffered GUI compositor (`rep stosq` / `rep movsq`) supporting z-order window focus, drop shadows, `[X]` close buttons, and an interactive 4-icon Dock (`SH`, `TM`, `SI`, `FS`).
- Built 4 independent desktop applications:
  - **Interactive Shell (`SH`)** with 10-line word-wrapped scrollback history
  - **Task Manager (`TM`)** with live Physical RAM, Kernel Heap (`B`), and Uptime telemetry
  - **System Info (`SI`)** displaying architecture, paging, and hardware bus status
  - **VFS Notes & File Explorer (`FS`)** with clickable file tabs

## 2. Dynamic Heap-Backed Virtual File System (VFS)
- Designed a 16-slot in-memory virtual file system dynamically backed by the kernel heap (`kmalloc` / `kfree`).
- Implemented interactive shell commands including:
  - `ls`, `cat <file>`, `write <file> <text>`, `touch <file>`, `rm <file>`
  - `lspci`, `ps`, `mem`, `uptime`, `uname`, `sysinfo`, `notes`, `clear`, `help`

## 3. UEFI Memory Map PMM, 2 MB Huge-Page VMM & Kernel Heap
- Implemented a Physical Memory Manager (PMM) that parses the live UEFI Memory Map descriptors, protects critical low memory and the kernel image, and reclaims `EfiBootServicesCode`/`Data` after transition.
- Built a 4-level Virtual Memory Manager (VMM) that identity-maps `0–4 GB` using **2 MB Huge Pages** (~20 KB of page tables) and splits 2 MB pages into 4 KB pages on demand for uncacheable MMIO mappings.
- Added a custom GDT, 64-bit TSS with **IST1 Double-Fault stack**, and 32 register-dumping CPU exception handlers.

## 4. Bare-Metal USB 3.0 (xHCI) & PCI Bus Drivers
- Built a bare-metal PCI bus enumerator (`lspci`) and USB 3.0 xHCI driver with command/event/transfer ring management, Link TRB cycle-bit wrapping, and 1 ms polling for USB HID keyboards, mice, and 1:1 absolute tablets.

## 5. PIT-Calibrated TSC Timer & Hardware RTC Clock
- Calibrated the CPU `rdtsc` counter against PIT channel 2 at boot for hardware-independent millisecond timeouts (`mdelay` / `timer_ms`), paired with a cached CMOS RTC driver displaying live **IST (`+05:30`)** time in the desktop top bar.

---

# Engineering Challenges & Trade-offs

One of the major architectural decisions involved choosing between:

## Polling Architecture vs Interrupt-Driven Architecture

### Initial Approach
The system initially used a polling-based architecture for handling keyboard and mouse events.

### Optimization Attempt
An interrupt-driven implementation using APIC and CPU sleep instructions (`hlt`) was explored to reduce unnecessary CPU utilization.

### Challenge Encountered
In the bare-metal virtualized environment, interrupt handling became unstable and caused occasional system freezes.

### Final Decision
To prioritize stability and maintain reliable system behavior, the architecture was rolled back to a stable polling-based implementation.

### Key Learning
This project reinforced the importance of:
- stability over premature optimization
- pragmatic engineering trade-offs
- reliable delivery under technical constraints

---

# Technical Concepts Explored

- Operating System Architecture
- Memory Management
- Low-Level Hardware Interaction
- Bare-Metal Boot Process
- Kernel Development
- Shell Design
- Virtual File Systems
- GUI Rendering
- Input Handling
- Resource Management
- Systems Debugging

---

# Why This Project Matters

This project demonstrates:
- first-principles problem solving
- persistence in handling complex systems
- ability to learn independently
- structured debugging and architectural thinking
- practical implementation under constrained environments

The project also reflects strong curiosity toward understanding systems beyond high-level software abstractions.

---

# How to Run

## Prerequisites
Install:
- clang
- lld
- qemu-system-x86_64

## Build & Run

```bash
chmod +x run.sh
./run.sh
```

## Test Features
- Drag GUI windows using mouse input.
- Open the shell and execute:

```bash
ls
```

- Read virtual file contents:

```bash
cat resume.txt
```

---

# Folder Structure Documentation

```text
SubhashishOS/
├── dist/                    # Generated binaries and UEFI boot image
│   ├── kernel.bin           # Flat 64-bit OS kernel binary (linked at 16MB)
│   └── EFI/BOOT/BOOTX64.EFI # UEFI bootloader application
├── src/stage1/              # Core kernel source files
│   ├── start.c              # Assembly bootstrap (.bss zeroing & 64KB stack setup)
│   ├── kernel.c             # Core kernel initialization and 60 FPS event loop
│   ├── gdt.c                # Kernel GDT & TSS (with Double-Fault IST stack)
│   ├── idt.c                # 32 CPU exception handlers & crash dump diagnostics
│   ├── timer.c              # PIT-calibrated TSC hardware timer & bounded delays
│   ├── pmm.c                # UEFI Memory Map-driven Physical Memory Manager
│   ├── vmm.c                # 4-level paging with 2MB huge-page identity mapping
│   ├── heap.c               # Dynamic kernel heap (kmalloc / kfree)
│   ├── acpi.c               # ACPI XSDT & MADT table parser
│   ├── apic.c               # Local APIC controller driver
│   ├── pci.c                # PCI bus enumerator & MSI configuration
│   ├── xhci.c               # USB 3.0 xHCI driver (HID keyboard & mouse)
│   ├── video.c              # Stride-aware double-buffered graphics engine
│   ├── gui.c                # Window manager, Task Manager, and interactive shell
│   ├── vfs.c                # In-memory Virtual File System
│   ├── rtc.c                # CMOS Real-Time Clock driver (IST timezone)
│   ├── lib.c                # Freestanding memory and string utilities
│   ├── kernel.ld            # Kernel linker script (0x1000000 base)
│   └── *.h                  # Subsystem headers and shared BootInfo contract
├── su_bootloader/           # Custom 2-stage UEFI bootloader
│   ├── boot.c               # UEFI entry point shim
│   ├── payload.c            # GOP, ACPI, Memory Map & ExitBootServices loader
│   ├── payload_data.s       # Embedded payload binary wrapper
│   └── linker.ld            # Payload linker script
├── run.sh                   # Build and QEMU execution script
└── PORTFOLIO.md             # Project case-study documentation
```

---

# High-Level System Architecture

```text
                +----------------------+
                |      System Boot Initialization   |
                +----------+-----------+
                           |
                           v
                +----------------------+
                |   Custom Bootloader  |
                |  (su_bootloader)     |
                +----------+-----------+
                           |
                           v
                +----------------------+
                |    Kernel Entry      |
                |     (entry.asm)      |
                +----------+-----------+
                           |
          +----------------+----------------+
          |                                 |
          v                                 v
+-------------------+          +----------------------+
| Memory Management |          | Interrupt Handling   |
| PMM / VMM / Heap  |          | IDT / APIC / ACPI   |
+-------------------+          +----------------------+
          |                                 |
          +----------------+----------------+
                           |
                           v
                +----------------------+
                |   Core Kernel Layer  |
                +----------+-----------+
                           |
        +------------------+------------------+
        |                                     |
        v                                     v
+-------------------+          +----------------------+
|  Virtual File     |          | Device Drivers       |
|  System (VFS)     |          | PCI / xHCI / RTC     |
+-------------------+          +----------------------+
        |                                     |
        +------------------+------------------+
                           |
                           v
                +----------------------+
                | GUI & Shell Layer    |
                | Windows / Shell / UI |
                +----------------------+
```

---

# Project Learnings

This project provided practical exposure to several advanced systems engineering concepts:

## 1. Managing Complexity Through Modularity
Breaking the operating system into isolated subsystems such as memory management, interrupt handling, drivers, and GUI rendering significantly improved debugging and maintainability.

## 2. Stability vs Optimization Trade-offs
Attempting interrupt-driven event handling highlighted how aggressive optimization can reduce system reliability in low-level environments. Prioritizing stable execution proved more valuable than premature optimization.

## 3. Importance of Structured Debugging
Kernel-level debugging required systematic isolation of faults, careful memory inspection, and iterative testing inside QEMU.

## 4. Hardware Abstractions Matter
Direct interaction with hardware components such as APIC, RTC, PCI, and xHCI controllers reinforced the complexity hidden beneath modern operating systems.

## 5. User Experience Influences Perceived Quality
Adding GUI components, draggable windows, and system telemetry significantly improved usability and made the system easier to interact with.

## 6. Independent Learning & Persistence
Developing the project required learning concepts beyond standard academic coursework, including low-level boot processes, linker scripts, memory mapping, and hardware communication.

---

# Suggested Evidence to Include

To strengthen project presentation during placements/interviews:

- GUI screenshots
- Boot sequence screenshots
- QEMU demo video
- System architecture diagram
- GitHub repository link
- README documentation

---

# Resume Version (Short Form)

## SubhashishOS — Custom Operating System Kernel
- Built a custom 64-bit operating system from scratch using C and x86_64 Assembly to explore kernel architecture and hardware interaction.
- Implemented memory management, an interactive shell, a virtual file system, and a lightweight graphical window manager.
- Developed hardware-level components including RTC clock drivers and system telemetry dashboards using custom bare-metal boot workflows.
- Evaluated architectural trade-offs between polling and interrupt-driven event handling to improve system stability.

---

# Interview Explanation (2-Minute Version)

"I built SubhashishOS to understand how operating systems work beyond high-level abstractions. Instead of relying on existing kernels, I implemented core components like memory management, a shell, a virtual file system, and a graphical interface from scratch using C and Assembly. One of the most valuable parts of the project was handling architectural trade-offs — particularly balancing interrupt-driven optimization against overall system stability in a bare-metal environment. The project helped me develop structured problem-solving, debugging discipline, and systems-level thinking."

