# Android Device tree for the Redmi 12

```
#
# Copyright (C) 2023 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#
```

## Fire GKI 6.6 layout

Build with `FIRE_KERNEL_VERSION=6.6` to use the Android header v4 GKI path.
The device tree asks the kernel module project to export a ROM dist, then lets
the normal Android build rules create `boot.img`, `vendor_boot.img`, `dtbo.img`,
and the vendor image.

- `boot.img` contains the generic GKI kernel image and keeps the boot cmdline
  empty.
- `vendor_boot.img` contains Fire-specific boot data: DTB, vendor cmdline,
  bootconfig, first-stage fstab, recovery ramdisk fragments, GSI AVB keys, and
  only the modules listed in `kernel/6.6/vendor_boot.modules.load`.
- The vendor image receives the complete module set from
  `kernel/6.6/vendor-modules.list`; its `modules.load` excludes the first-stage
  vendor_boot modules.

Stock Fire LK was observed to read AVB-requested partitions by their physical
GPT sizes while unlocked. The v4 GKI layout therefore requires the real
`vendor_boot_a` and `vendor_boot_b` GPT entries to match the configured
8 MiB `BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE`; leaving the stock 64 MiB
vendor_boot entries can exhaust LK AVB heap before Linux starts.
This tree carries `kernel/6.6/MT6768_Android_scatter.xml` with the matching
physical layout. The 6.6 kernel dist target automatically validates it and
exports a corrected scatter copy next to the kernel artifacts, so no extra
build flags are needed for the standard Fire 6.6 path. Check a candidate
external scatter before release with:

```sh
python3 device/xiaomi/fire/kernel/6.6/check-fire-gki66-partitions.py MT6768_Android_scatter.xml
```

`FIRE_KERNEL_6_6_SCATTER_SOURCE` is only an override for testing another
firmware tree.

The recovery install flow remains compatible with the crDroid instructions:
flash recovery through `fastboot flash vendor_boot vendor_boot.img`, then reboot
to recovery and sideload the ROM. The recovery ZIP delivers the rebuilt boot,
vendor_boot, dtbo, vbmeta, and vendor module payloads after the device already
has the corrected 8 MiB `vendor_boot_a`/`vendor_boot_b` GPT layout; a normal
A/B sideload or updater package is not the first-time GPT migration mechanism.
