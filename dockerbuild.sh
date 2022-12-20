#/bin/bash

docker build . -t mtk-eip93-build

container=$(docker create mtk-eip93-build bash -c 'cd /root && ./build.sh')
docker cp crypto/mtk-eip93 "$container":/root/kernel/drivers/crypto/mtk-eip93
docker cp ./999-add-eip93-crypto.patch "$container":/root/kernel/999-add-eip93-crypto.patch
docker cp build.sh "$container":/root/build.sh
docker start -ai "$container"
docker cp "$container":/root/kernel/drivers/crypto/mtk-eip93/crypto-hw-eip93.ko .
docker rm "$container"
