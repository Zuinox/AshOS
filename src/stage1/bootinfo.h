#ifndef BOOTINFO_H
#define BOOTINFO_H

// The "briefcase" handed from the bootloader (su_bootloader/payload.c) to the
// kernel. The layout MUST match the copy of this struct in payload.c.

#define BOOTINFO_MAGIC 0x5355424FU /* 'SUBO' */

typedef struct {
    void* FramebufferBase;
    unsigned long long FrameBufferSize;
    unsigned int Width;
    unsigned int Height;
    unsigned int PixelsPerScanLine;

    void* MemoryMap;
    unsigned long long MemoryMapSize;
    unsigned long long DescriptorSize;

    void* AcpiRsdp;

    unsigned int PixelFormat; // EFI_GRAPHICS_PIXEL_FORMAT: 0 = RGBX, 1 = BGRX
    unsigned int Magic;       // BOOTINFO_MAGIC, used to validate the hand-off
} BootInfo;

// UEFI memory map entry (EFI_MEMORY_DESCRIPTOR). Always iterate using
// BootInfo.DescriptorSize as the stride, never sizeof(efi_memory_descriptor_t).
typedef struct {
    unsigned int Type;
    unsigned int Pad;
    unsigned long long PhysicalStart;
    unsigned long long VirtualStart;
    unsigned long long NumberOfPages;
    unsigned long long Attribute;
} efi_memory_descriptor_t;

#define EFI_LOADER_CODE           1
#define EFI_LOADER_DATA           2
#define EFI_BOOT_SERVICES_CODE    3
#define EFI_BOOT_SERVICES_DATA    4
#define EFI_CONVENTIONAL_MEMORY   7

// The kernel is loaded at exactly this address (see src/stage1/kernel.ld)
// and the bootloader reserves KERNEL_MAX_SIZE bytes for image + .bss.
#define KERNEL_PHYS_BASE 0x1000000ULL
#define KERNEL_MAX_SIZE  (4ULL * 1024 * 1024)

#endif
