// su_bootloader/boot.c
typedef void* EFI_HANDLE;
typedef unsigned long long EFI_STATUS;

extern unsigned char _payload_start[];

EFI_STATUS __attribute__((ms_abi)) efi_main(EFI_HANDLE ImageHandle, void *SystemTable) {
    // Update the function pointer to accept TWO arguments
    void (*entry_point)(EFI_HANDLE, void *) = (void (*)(EFI_HANDLE, void *))_payload_start;
    
    // Pass the Handle and the Table!
    entry_point(ImageHandle, SystemTable);

    return 0; 
}