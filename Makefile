CC=gcc
LD=ld
CFLAGS_INIT = -c -std=gnu11 -ffreestanding -fno-stack-protector -fno-stack-check -fno-lto -mgeneral-regs-only \
	-fPIE -m64 -march=x86-64 -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone
CINCLUDES = -I include

KRNL_SRC        = $(wildcard krnl/*.c) $(wildcard krnl/**/*.c) $(wildcard krnl/**/**/*.c)
KRNL_INC    = -I krnl/
KRNL_BUILD      = build


.PHONY: build kernel iso run initramfs

build: kernel
	@echo "Build complete successfully."

initramfs:
	@mkdir -p initramfs/dev
	@cd initramfs && tar -H ustar -cf ../initramfs.tar *
	
kernel: $(KRNL_SRC)
	@sh scripts/update_build_counter.sh
	@mkdir -p $(KRNL_BUILD)/krnl
	@for src in $(KRNL_SRC); do \
		base=$${src##*/}; \
		echo "[CC] Compiling $$base..."; \
		$(CC) $(CFLAGS_INIT) $(CINCLUDES) $(KRNL_INC) $$src -o $(KRNL_BUILD)/krnl/$$base.o; \
	done
	@$(LD) $(KRNL_BUILD)/krnl/*.o -o kernel.elf -T krnl/linker.ld -nostdlib -static -pie --no-dynamic-linker -z text -z max-page-size=0x1000
	@echo "[LD] Linking kernel.elf..."

iso: kernel initramfs
	mkdir -p iso_root/boot
	mkdir -p iso_root/EFI/BOOT

	cp kernel.elf iso_root/boot/
	cp initramfs.tar iso_root/boot/
	cp limine.conf iso_root/boot/

	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/

	xorriso -as mkisofs -b boot/limine-bios-cd.bin \
			-no-emul-boot -boot-load-size 4 -boot-info-table \
			--efi-boot boot/limine-uefi-cd.bin \
			-efi-boot-part --efi-boot-image --protective-msdos-label \
			iso_root -o muntos.iso

	./limine/limine bios-install muntos.iso

run: iso
	qemu-system-x86_64 -cdrom muntos.iso -d int -no-reboot -D qemu.log \
		-accel kvm -cpu host -smp sockets=1,cores=8,threads=1 \
		-M q35 -m 256M -serial stdio -device pci-ohci

run-host: iso
	qemu-system-x86_64 -cdrom muntos.iso -d int -no-reboot -D qemu.log \
		-accel kvm -cpu host -smp sockets=1,cores=8,threads=1 \
		-M q35 -m 256M -serial stdio \
		-drive if=none,id=stick,file=kernel.elf,format=raw \
		-drive if=none,id=sda,file=initramfs.tar,format=raw \
		-device ahci,id=ahci \
		-device ide-hd,drive=sda,bus=ahci.0 \
		-device pci-ohci -device usb-kbd \
		-device qemu-xhci -device usb-mouse -device usb-storage,drive=stick \
		-device virtio-gpu-pci

prepare-sdk:
	@echo "Preparing SDK..."
	@cp krnl/api/sysnums.h munt3os-sdk/sys/
	@cp krnl/api/sysdef.h munt3os-sdk/sys/

clean:
	rm -rf build/*
	rm -f kernel.elf
	rm -f muntos.iso

OBJS = \
    build/krnl/boot0.o \
    build/krnl/cstdlib.o \
    build/krnl/font6x8.o \
    build/krnl/kernel.o \
	build/krnl/kpanic.o \
    build/krnl/log.o \
    build/krnl/memio.o \
    build/krnl/mm.o \
    build/krnl/printf.o \
    build/krnl/tty.o \
    build/krnl/dev/null.o \
    build/krnl/dev/random.o \
    build/krnl/dev/zero.o \
    build/krnl/fs/devfs.o \
    build/krnl/fs/tarfs.o \
	build/krnl/fs/ext2.o \
    build/krnl/fs/vfs.o \
    build/krnl/hw/acpi.o \
    build/krnl/hw/COM.o \
    build/krnl/hw/cpuid.o \
    build/krnl/hw/idt.o \
    build/krnl/hw/pcie.o \
    build/krnl/hw/ps2.o \
    build/krnl/hw/video.o \
    build/krnl/hw/usb/usb.o \
    build/krnl/mem/kbuddy.o \
    build/krnl/mem/kheap.o \