#!/bin/sh
tar -xvf libvterm-0.99.7.tar.gz
cd libvterm

patch -p1 -i ../libvterm.patch

make

cp libvterm.a ../
cp vterm.h ../
