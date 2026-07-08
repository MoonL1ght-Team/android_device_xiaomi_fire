#
# Copyright (C) 2026 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

FIRE_KERNEL_VERSION ?= 4.19
FIRE_KERNEL_6_6_VENDOR_MODULE_LIST ?= device/xiaomi/fire/kernel/6.6/vendor-modules.list

ifeq ($(FIRE_KERNEL_VERSION),6.6)
PRODUCT_PACKAGES += fire_kernel_6_6_vendor_modules
endif
