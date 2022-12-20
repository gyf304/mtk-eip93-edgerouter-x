FROM debian:buster-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
	build-essential \
	curl \
	git \
	gcc-mipsel-linux-gnu \
	bc

# Get kernel
WORKDIR /root
RUN \
	curl -kO https://dl.ui.com/firmwares/edgemax/v2.0.9-hotfix.2/gpl/GPL.ER-e50.v2.0.9-hotfix.2.5402463.tar.bz2 && \
	tar -xvf GPL.ER-e50.v2.0.9-hotfix.2.5402463.tar.bz2 source/kernel_5402463-gdccea122ed61.tgz && \
	tar -xvf source/kernel_5402463-gdccea122ed61.tgz && \
	rm -rf *.bz2 source

# prepare kernel
