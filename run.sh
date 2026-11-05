#!/bin/bash
set -e # Stop immediately if any command fails

# --- THE FIX: Clean up ghosts from previous failed builds ---
rm -f *.o payload.bin

BOOT_SRC="su_bootloader"
KERNEL_SRC="src/stage1"
OUTPUT_DIR="dist/EFI/BOOT"

# Ensure output directories exist
mkdir -p $OUTPUT_DIR

# Find LLVM Linker for macOS
LD_LLD=$(command -v ld.lld || echo "/opt/homebrew/opt/llvm/bin/ld.lld")
if [ ! -f "$LD_LLD" ]; then
    echo "ERROR: ld.lld not found. Is llvm installed via brew?"
    exit 1
fi

echo "========================================"
echo "      BUILDING SUBHASHISH OS"
echo "========================================"

echo ""
echo "--- STEP 1: COMPILING THE KERNEL ---"
# Added -Wno-excessive-regsave to silence the ISR warnings
clang -target x86_64-unknown-none-elf \
      -ffreestanding -fshort-wchar -mno-red-zone \
      -mno-sse -mno-mmx -mno-sse2 -mno-avx -mgeneral-regs-only \
      -Wno-excessive-regsave \
      -fno-stack-protector -fPIC -O2 \
      -c $KERNEL_SRC/*.c

# Link the kernel using src/stage1/kernel.ld (anchored at 0x1000000 with explicit .bss)
$LD_LLD -T $KERNEL_SRC/kernel.ld --oformat=binary *.o -o dist/kernel.bin

# Clean up kernel object files so they don't mix with the bootloader
rm -f *.o 

echo "--- STEP 2: COMPILING THE PAYLOAD ---"
clang -target x86_64-unknown-none-elf \
      -ffreestanding -fshort-wchar -mno-red-zone \
      -mno-sse -mno-mmx -mno-sse2 -mno-avx \
      -fno-stack-protector -fPIC -O2 \
      -c $BOOT_SRC/payload.c -o payload.o

$LD_LLD -T $BOOT_SRC/linker.ld --oformat=binary payload.o -o payload.bin

echo "--- STEP 3: EMBEDDING PAYLOAD IN SHIM ---"
nasm -f win64 $BOOT_SRC/payload_data.s -o payload_data.o

echo "--- STEP 4: FINAL UEFI LINK ---"
clang -target x86_64-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone \
      -c $BOOT_SRC/boot.c -o boot.o

lld-link -subsystem:efi_application -nodefaultlib -entry:efi_main \
         boot.o payload_data.o -out:$OUTPUT_DIR/BOOTX64.EFI

echo "--- CLEANUP ---"
rm -f *.o payload.bin

echo ""
echo "========================================"
echo " SUCCESS! FILES GENERATED:"
echo " 1. dist/kernel.bin       (The OS Kernel)"
echo " 2. dist/EFI/BOOT/BOOTX64.EFI (The Bootloader)"
echo "========================================"

echo "--- OS STARTING ---"
# Locate UEFI firmware in a version-independent way (survives `brew upgrade qemu`)
OVMF_CODE=""
for candidate in \
    "$(brew --prefix qemu 2>/dev/null)/share/qemu/edk2-x86_64-code.fd" \
    "/opt/homebrew/share/qemu/edk2-x86_64-code.fd" \
    "/usr/local/share/qemu/edk2-x86_64-code.fd"; do
    if [ -f "$candidate" ]; then OVMF_CODE="$candidate"; break; fi
done
if [ -z "$OVMF_CODE" ]; then
    echo "ERROR: edk2-x86_64-code.fd not found. Is qemu installed via brew?"
    exit 1
fi
echo "Using firmware: $OVMF_CODE"

# Remove stale monitor socket from a previous run
rm -f qemu-monitor-socket

qemu-system-x86_64 \
    -m 512M \
    -machine q35 \
    -cpu qemu64 \
    -device virtio-gpu-pci \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0,port=1 \
    -device usb-tablet,bus=xhci.0,port=2 \
    -serial stdio \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive file=fat:rw:dist,format=raw \
    -monitor unix:qemu-monitor-socket,server,nowait \
    -display cocoa