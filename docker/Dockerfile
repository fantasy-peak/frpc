ARG ROCKY_LINUX_VERSIONS

FROM rockylinux:${ROCKY_LINUX_VERSIONS}

SHELL ["/bin/bash", "-c"]

RUN update-ca-trust; \
	rm -f /etc/localtime; \
	ln -s /usr/share/zoneinfo/Asia/Tokyo /etc/localtime; \
	# dnf update -y; \
	dnf install -y \
		autoconf \
		automake \
		bison \
		bzip2 \
		cmake \
		git \
		libtool \
		make \
		openssh.x86_64 \
		openssh-clients \
		patch \
		rpm-build \
		tar \
		wget \
		libuuid-devel.x86_64 \
		clang-tools-extra.x86_64 \
		gcc-toolset-13* \
		which \
		yum-utils; \
	dnf clean all

RUN echo "source /opt/rh/gcc-toolset-13/enable" >> ~/.bashrc

WORKDIR /root

RUN source ~/.bashrc \
	&&curl -kfsSL https://xmake.io/shget.text | bash -s v2.9.1 \
	&& echo "export XMAKE_ROOT=y" >> ~/.bashrc \
	&& echo "source ~/.xmake/profile"  >> ~/.bashrc

RUN source ~/.bashrc \
	&& xmake --version \
	&& git clone https://github.com/fantasy-peak/frpc.git \
	&& cd frpc \
	&& xmake build -v -y --file=./xmake.lua \
	&& xmake install -o . --file=./xmake.lua \
	&& ./bin/frpc -f ./config/config.yaml -t ./template/cpp -o ./out -w ./template/web --auto_sort=0 --debug=0 \
	&& xmake build -v -y --file=./test_xmake.lua \
	&& xmake install -o . --file=./test_xmake.lua
