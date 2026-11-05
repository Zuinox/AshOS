// su_bootloader/payload.c
typedef unsigned short CHAR16;
typedef unsigned long long UINTN;
typedef unsigned long long EFI_STATUS;

typedef struct {
  unsigned int Data1;
  unsigned short Data2;
  unsigned short Data3;
  unsigned char Data4[8];
} EFI_GUID;
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                      \
  {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                   \
  {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

typedef struct {
  unsigned int Version;
  unsigned int HorizontalResolution;
  unsigned int VerticalResolution;
  unsigned int PixelFormat;
  unsigned int PixelInformation[4];
  unsigned int PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
  unsigned int MaxMode;
  unsigned int Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  void *FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct {
  void *QueryMode;
  void *SetMode;
  void *Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct _EFI_FILE_PROTOCOL {
  UINTN Revision;
  EFI_STATUS(__attribute__((ms_abi)) *
             Open)(struct _EFI_FILE_PROTOCOL *This,
                   struct _EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName,
                   UINTN OpenMode, UINTN Attributes);
  EFI_STATUS(__attribute__((ms_abi)) * Close)(struct _EFI_FILE_PROTOCOL *This);
  EFI_STATUS(__attribute__((ms_abi)) * Delete)(struct _EFI_FILE_PROTOCOL *This);
  EFI_STATUS(__attribute__((ms_abi)) * Read)(struct _EFI_FILE_PROTOCOL *This,
                                             UINTN *BufferSize, void *Buffer);
} EFI_FILE_PROTOCOL;

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  UINTN Revision;
  EFI_STATUS(__attribute__((ms_abi)) *
             OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
                         EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
  unsigned int Type;
  void *PhysicalStart;
  void *VirtualStart;
  unsigned long long NumberOfPages;
  unsigned long long Attribute;
} EFI_MEMORY_DESCRIPTOR;
typedef struct {
  EFI_GUID VendorGuid;
  void *VendorTable;
} EFI_CONFIGURATION_TABLE;

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS(__attribute__((ms_abi)) * EFI_TEXT_STRING)(void *This,
                                                              CHAR16 *String);
typedef struct {
  void *Reset;
  EFI_TEXT_STRING OutputString;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// Struct explicitly padded out to prevent LocateProtocol offset bugs
typedef struct {
  char Header[24];
  void *RaiseTPL;
  void *RestoreTPL;
  EFI_STATUS(__attribute__((ms_abi)) *
             AllocatePages)(int Type, int MemoryType, UINTN Pages,
                            unsigned long long *Memory);
  void *FreePages;
  EFI_STATUS(__attribute__((ms_abi)) *
             GetMemoryMap)(UINTN *MemoryMapSize,
                           EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey,
                           UINTN *DescriptorSize,
                           unsigned int *DescriptorVersion);
  EFI_STATUS(__attribute__((ms_abi)) * AllocatePool)(int PoolType, UINTN Size,
                                                     void **Buffer);
  EFI_STATUS(__attribute__((ms_abi)) * FreePool)(void *Buffer);
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  void *HandleProtocol;
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  void *LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  EFI_STATUS(__attribute__((ms_abi)) * ExitBootServices)(void *ImageHandle,
                                                         UINTN MapKey);
  void *GetNextMonotonicCount;
  void *Stall;
  void *SetWatchdogTimer;
  void *ConnectController;
  void *DisconnectController;
  void *OpenProtocol;
  void *CloseProtocol;
  void *OpenProtocolInformation;
  void *ProtocolsPerHandle;
  void *LocateHandleBuffer;
  EFI_STATUS(__attribute__((ms_abi)) * LocateProtocol)(EFI_GUID *Protocol,
                                                       void *Registration,
                                                       void **Interface);
} EFI_BOOT_SERVICES;

typedef struct {
  char Header[24];
  void *Vendor;
  unsigned int Rev;
  unsigned int Pad;
  void *InH;
  void *In;
  void *OutH;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  void *ErrH;
  void *Err;
  void *Runtime;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

#define BOOTINFO_MAGIC 0x5355424FU /* 'SUBO' */

typedef struct {
  void *FramebufferBase;
  UINTN FrameBufferSize;
  unsigned int Width;
  unsigned int Height;
  unsigned int PixelsPerScanLine;
  void *MemoryMap;
  UINTN MemoryMapSize;
  UINTN DescriptorSize;
  void *AcpiRsdp;
  unsigned int PixelFormat;
  unsigned int Magic;
} BootInfo;

int CompareGuid(EFI_GUID *g1, EFI_GUID *g2) {
  if (g1->Data1 != g2->Data1 || g1->Data2 != g2->Data2 ||
      g1->Data3 != g2->Data3)
    return 0;
  for (int i = 0; i < 8; i++)
    if (g1->Data4[i] != g2->Data4[i])
      return 0;
  return 1;
}

#if defined(__APPLE__)
#define ENTRY_SECTION __attribute__((section("__TEXT,__text")))
#else
#define ENTRY_SECTION __attribute__((section(".text.entry")))
#endif

ENTRY_SECTION __attribute__((ms_abi)) void
payload_main(void *ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  if (!SystemTable || !SystemTable->ConOut)
    while (1)
      ;

  BootInfo briefcase;
  briefcase.FramebufferBase = 0;
  briefcase.FrameBufferSize = 0;
  briefcase.Width = 0;
  briefcase.Height = 0;
  briefcase.PixelsPerScanLine = 0;
  briefcase.MemoryMap = 0;
  briefcase.MemoryMapSize = 0;
  briefcase.DescriptorSize = 0;
  briefcase.AcpiRsdp = 0;
  briefcase.PixelFormat = 0;
  briefcase.Magic = BOOTINFO_MAGIC;

  // A. ACPI & GRAPHICS
  EFI_GUID acpi2_guid = {0x8868e871,
                         0xe4f1,
                         0x11d3,
                         {0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
  for (UINTN i = 0; i < SystemTable->NumberOfTableEntries; i++) {
    if (CompareGuid(&SystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi2_guid)) {
      briefcase.AcpiRsdp = SystemTable->ConfigurationTable[i].VendorTable;
      break;
    }
  }

  EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
  SystemTable->BootServices->LocateProtocol(&gop_guid, 0, (void **)&gop);
  if (gop) {
    briefcase.FramebufferBase = gop->Mode->FrameBufferBase;
    briefcase.FrameBufferSize = gop->Mode->FrameBufferSize;
    briefcase.Width = gop->Mode->Info->HorizontalResolution;
    briefcase.Height = gop->Mode->Info->VerticalResolution;
    briefcase.PixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
    briefcase.PixelFormat = gop->Mode->Info->PixelFormat;
  }

  // B. LOAD KERNEL EXACTLY AT 16MB (0x1000000), RESERVE 4MB (1024 PAGES)
  SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                    (CHAR16 *)L"Mounting File System...\r\n");
  EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
  EFI_FILE_PROTOCOL *root = 0;
  EFI_FILE_PROTOCOL *kernel_file = 0;

  unsigned long long kernel_addr = 0x1000000; // 16MB target
  UINTN kernel_size = 4 * 1024 * 1024;

  if (SystemTable->BootServices->LocateProtocol(&fs_guid, 0, (void **)&fs) ==
          0 &&
      fs) {
    if (fs->OpenVolume(fs, &root) == 0 && root) {
      if (root->Open(root, &kernel_file, (CHAR16 *)L"kernel.bin", 1, 0) == 0 &&
          kernel_file) {
        SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                          (CHAR16 *)L" [OK] kernel.bin found.\r\n");

        // Type 1 = AllocateAddress, MemoryType 2 = EfiLoaderData, 1024 pages = 4MB
        EFI_STATUS alloc_status =
            SystemTable->BootServices->AllocatePages(1, 2, 1024, &kernel_addr);
        if (alloc_status == 0) {
          SystemTable->ConOut->OutputString(
              SystemTable->ConOut, (CHAR16 *)L" [OK] 16MB Memory Allocated.\r\n");

          if (kernel_file->Read(kernel_file, &kernel_size,
                                (void *)kernel_addr) == 0) {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut, (CHAR16 *)L" [OK] Kernel Loaded successfully!\r\n");
          } else {
            SystemTable->ConOut->OutputString(
                SystemTable->ConOut, (CHAR16 *)L" [FAIL] Error reading file.\r\n");
            while (1) {
              __asm__("hlt");
            }
          }
        } else {
          SystemTable->ConOut->OutputString(
              SystemTable->ConOut,
              (CHAR16 *)L" [FAIL] Memory Address 16MB is taken by UEFI.\r\n");
          while (1) {
            __asm__("hlt");
          }
        }
      } else {
        SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                          (CHAR16 *)L" [FAIL] kernel.bin missing.\r\n");
        while (1) {
          __asm__("hlt");
        }
      }
    }
  } else {
    SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                      (CHAR16 *)L" [FAIL] FS Protocol missing.\r\n");
    while (1) {
      __asm__("hlt");
    }
  }

  // C. HARVEST MEMORY MAP & D. EXIT BOOT SERVICES (WITH RETRY VERIFICATION)
  UINTN MapSize = 0;
  UINTN MapKey = 0;
  UINTN DescriptorSize = 0;
  unsigned int DescriptorVersion = 0;
  SystemTable->BootServices->GetMemoryMap(&MapSize, 0, &MapKey, &DescriptorSize,
                                          &DescriptorVersion);
  UINTN BufferCapacity = MapSize + (8 * DescriptorSize);
  SystemTable->BootServices->AllocatePool(2, BufferCapacity,
                                          (void **)&briefcase.MemoryMap);

  SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                    (CHAR16 *)L"Jumping to OS...\r\n");

  EFI_STATUS exit_status = 1;
  for (int attempt = 0; attempt < 4; attempt++) {
    MapSize = BufferCapacity;
    if (SystemTable->BootServices->GetMemoryMap(
            &MapSize, (EFI_MEMORY_DESCRIPTOR *)briefcase.MemoryMap, &MapKey,
            &DescriptorSize, &DescriptorVersion) != 0) {
      continue;
    }
    briefcase.MemoryMapSize = MapSize;
    briefcase.DescriptorSize = DescriptorSize;
    exit_status =
        SystemTable->BootServices->ExitBootServices(ImageHandle, MapKey);
    if (exit_status == 0) {
      break;
    }
  }
  if (exit_status != 0) {
    while (1) {
      __asm__("cli; hlt");
    }
  }

  // E. THE LEAP OF FAITH (Target uses SysV ABI: RDI = &briefcase)
  void (__attribute__((sysv_abi)) *kernel_entry)(BootInfo *) =
      (void (__attribute__((sysv_abi)) *)(BootInfo *))kernel_addr;
  kernel_entry(&briefcase);

  while (1) {
    __asm__("hlt");
  }
}