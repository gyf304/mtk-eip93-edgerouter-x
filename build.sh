#/bin/bash

set -e

function mk() {
	make \
	ARCH=mips \
	CROSS_COMPILE=mipsel-linux-gnu- \
	$@
}

pushd kernel

patch -s -p1 < 999-add-eip93-crypto.patch

cat drivers/crypto/Kconfig

mk defconfig KBUILD_DEFCONFIG=ubnt_er_e50_defconfig
cat >> .config << EOF
CONFIG_CRYPTO_HW=y
CONFIG_CRYPTO_DEV_EIP93=m
CONFIG_CRYPTO_DEV_EIP93_SKCIPHER=y
CONFIG_CRYPTO_DEV_EIP93_AES=y
CONFIG_CRYPTO_DEV_EIP93_DES=n
CONFIG_CRYPTO_DEV_EIP93_AEAD=n
EOF
mk oldconfig

mk modules_prepare
mk M=drivers/crypto/mtk-eip93 modules

popd
