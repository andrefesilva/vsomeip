FROM ubuntu:noble
SHELL ["/bin/bash", "-xec"]

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update;\
    apt-get install --no-install-recommends --yes\
        bind9-dnsutils\
        bubblewrap \
        cmake\
        g++\
        gdb\
        git\
        googletest\
        iptables\
        iproute2\
        iputils-ping\
        jq\
        lcov\
        libboost-all-dev \
        libnetfilter-queue-dev\
        libnfnetlink-dev\
        lsof\
        net-tools\
        ninja-build\
        openssh-server\
        pkg-config\
        python3-dev\
        python3-pip\
        python3-scapy\
        tcpdump\
        valgrind\
        wget\
        socat \
        strace \
        linux-tools-common \
        linux-tools-generic \
        perl \
        ;\
    apt-get autoremove --purge --yes;\
    apt-get clean;\
    pip3 install --break-system-packages gcovr NetfilterQueue

# FlameGraph for perf-based flamegraph generation
RUN git clone --depth 1 https://github.com/brendangregg/FlameGraph /opt/flamegraph

# Default GCC-13 from Noble
ENV CC=gcc
ENV CXX=g++
ENV GCOV=gcov

# /etc configurations
RUN echo 'SendEnv *' > /etc/ssh/ssh_config.d/vsomeip.conf;\
    echo 'AcceptEnv *' > /etc/ssh/sshd_config.d/vsomeip.conf

# SSH configurations
RUN ssh-keygen -q -b 1024 -f ~/.ssh/id_rsa -t rsa -N '';\
    install -DTm0400 ~/.ssh/id_rsa.pub ~/.ssh/authorized_keys;\
    install -DTm0400 ~/.ssh/id_rsa /commonapi_main/lxc-config/.ssh/mgc_lxc/rsa_key_file.pub

# Add ssh_config to the container
COPY zuul/network-tests/ssh_config /root/.ssh/config
RUN chown root:root /root/.ssh/config && chmod 600 /root/.ssh/config

# Hard-coded path used in the tests' scripts
RUN ln -s /home/build /vsomeip_lib

# Place stuff in a known place
RUN git config --global --add safe.directory /home/source

WORKDIR /home/source
# Declare bind-mounts
VOLUME ["/home/build", "/home/logs", "/home/source"]
