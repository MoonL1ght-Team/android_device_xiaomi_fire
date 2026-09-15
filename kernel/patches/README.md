# crDroid kernel build compatibility

When building the kernel from source with prebuilt HOS2 DLKM images, crDroid's
kernel rules must not depend on DLKM file lists that are only generated when
building those partition images. Otherwise Ninja fails on `/file_list.txt`.
The patch keeps file-list tracking for images built from source and leaves it
empty for prebuilt images. The module installation helper already handles an
empty file-list argument.

After syncing the device tree, run from the ROM source root:

```sh
git -C vendor/lineage apply --check ../../device/xiaomi/fire/kernel/patches/0001-kernel-handle-prebuilt-dlkm-file-lists.patch
git -C vendor/lineage apply ../../device/xiaomi/fire/kernel/patches/0001-kernel-handle-prebuilt-dlkm-file-lists.patch
```

Apply once, then resume the build. No output cleanup is required.
