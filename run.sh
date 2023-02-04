# A handy way to run xv6 and automatically clean after this
if [[ $1 == "debug" ]]
then
  make qemu-gdb
else
  make qemu
fi
make clean