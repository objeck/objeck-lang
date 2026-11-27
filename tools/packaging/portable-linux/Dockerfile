# The image the Linux release is built in. Design: docs/design/PORTABLE_LINUX.md.
#
# AlmaLinux 8 has glibc 2.28 and libstdc++ 8, older than every distribution the
# release supports, so a binary linked here asks for nothing newer. GCC 14 comes
# from gcc-toolset-14, which links libstdc++.so.6 only up to what GCC 8 exported
# and puts the rest of the C++ runtime into the binary itself.
#
# Nothing here is shipped except what the build links statically. The -devel
# packages give the optional native libraries something to link against; at run
# time those libraries come from the user's system.
FROM almalinux:8.10

RUN dnf -y install epel-release dnf-plugins-core \
 && dnf config-manager --set-enabled powertools \
 && dnf -y install \
      gcc-toolset-14-gcc gcc-toolset-14-gcc-c++ gcc-toolset-14-binutils \
      make cmake git perl python3 pkgconf-pkg-config zlib-devel \
      curl ca-certificates tar gzip bzip2 xz unzip zip patch diffutils findutils file which \
      unixODBC-devel lame-devel mesa-libGL-devel \
      SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_ttf-devel \
      gtk3-devel gstreamer1-devel gstreamer1-plugins-base-devel \
 && dnf clean all

# gcc, g++, cc and c++ resolve to the wrapper, which adds the hardening defaults
# Ubuntu's compiler has built in (see the file).
COPY cc-wrapper.sh /opt/objeck-toolchain/bin/cc-wrapper.sh
RUN chmod 755 /opt/objeck-toolchain/bin/cc-wrapper.sh \
 && for c in gcc g++ cc c++; do ln -s cc-wrapper.sh /opt/objeck-toolchain/bin/$c; done

ENV PATH=/opt/objeck-toolchain/bin:/opt/rh/gcc-toolset-14/root/usr/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
