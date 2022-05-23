ifeq ($(CONFIG_BUILD_ARM64_DT_OVERLAY),y)
dtbo-$(CONFIG_ARCH_HOLI) += holi-oscar-21141-21341-overlay-EVT.dtbo
dtbo-$(CONFIG_ARCH_HOLI) += holi-oscar-21141-21341-overlay-DVT.dtbo
dtbo-$(CONFIG_ARCH_HOLI) += holi-oscar-21141-21341-overlay-PVT-EU.dtbo
dtbo-$(CONFIG_ARCH_HOLI) += holi-oscar-21141-21341-overlay-PVT.dtbo

holi-oscar-21141-21341-overlay-EVT.dtbo-base := holi-oscar.dtb
holi-oscar-21141-21341-overlay-DVT.dtbo-base := holi-oscar.dtb
holi-oscar-21141-21341-overlay-PVT-EU.dtbo-base := holi-oscar.dtb
holi-oscar-21141-21341-overlay-PVT.dtbo-base := holi-oscar.dtb
endif

always := $(dtb-y)
subdir-y := $(dts-dirs)
clean-files := *.dtb *.dtbo
