#!/bin/bash

kernel_version="4.14.54-UBNT"
of_compatible_path="/sys/devices/platform/1e004000.crypto/of_node/compatible"
of_compatible="mediatek,mtk-eip93"
ko_name="crypto-hw-eip93"
ko_file="$ko_name.ko"

if [ "$UID" != "0" ]; then
	echo "This script must be run as root"
	exit 1
fi

if [ "$(uname -r)" != "$kernel_version" ]; then
	echo "Incompatible kernel detected"
	exit 1
fi

if [ $(cut -d '' -f 1 "$of_compatible_path") != "$of_compatible" ]; then
	echo "Incompatible device tree detected"
	exit 1
fi

if [ ! -f "$ko_file" ]; then
	echo "Kernel module $ko_file not found"
	exit 1
fi

mkdir -p "/lib/modules/$kernel_version/kernel/drivers/crypto/mtk-eip93"
cp "$ko_file" "/lib/modules/$kernel_version/kernel/drivers/crypto/mtk-eip93/$ko_file"
chmod 644 "/lib/modules/$kernel_version/kernel/drivers/crypto/mtk-eip93/$ko_file"
depmod -a
modprobe "$ko_name"
echo "Kernel module $ko_name installed"
