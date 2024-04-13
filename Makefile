MKFILE_PATH = $(abspath $(firstword $(MAKEFILE_LIST)))
MKFILE_DIR = $(patsubst %/,%,$(dir $(MKFILE_PATH)))

K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/dev/uart.o \
  $K/mem/kalloc.o \
  $K/util/spinlock.o \
  $K/util/string.o \
  $K/main.o \
  $K/mem/vm.o \
  $K/proc/proc.o \
  $K/proc/swtch.o \
  $K/proc/trampoline.o \
  $K/proc/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/fs/bio.o \
  $K/fs/fs.o \
  $K/fs/log.o \
  $K/util/sleeplock.o \
  $K/fs/file.o \
  $K/pipe.o \
  $K/proc/exec.o \
  $K/fs/sysfile.o \
  $K/kernelvec.o \
  $K/dev/plic.o \
  $K/dev/virtio_disk.o \
  $K/mem/pool_alloc.o \
  $K/mem/buddy_alloc.o \
  $K/util/bitset.o \
  $K/util/free_mem_list.o \
  $K/util/vector.o \
  $K/proc/free_proc_pool.o \
  $K/proc/kstack_provider.o \
  $K/util/rw_lock.o \
  $K/dev/device_tree.o

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64

CC = $(TOOLPREFIX)gcc
CXX = $(TOOLPREFIX)g++
AS = $(TOOLPREFIX)as
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -O0 -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

$K/kernel: $(OBJS) $K/kernel.ld $U/initcode
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS) 
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm

tags: $(OBJS) _init
	etags *.S *.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

$U/%.o : $U/%.cpp
	$(CC) $(CFLAGS) -x c++ -c -o $@ $<

$U/%.o : $U/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

mkfs/make_fs: mkfs/make_fs.cpp $K/fs/fs.h $K/param.h
	g++ -Werror -Wall -I. -o mkfs/make_fs mkfs/make_fs.cpp

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_alloctest\

all_user: $(UPROGS)

fs.img: mkfs/make_fs README $(UPROGS)
	mkfs/make_fs fs.img README $(UPROGS)

-include kernel/*.d user/*.d

clean:
	find . -regextype posix-egrep -regex ".*\.(o|d|asm|sym)" -type f -delete
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	$U/initcode $U/initcode.out $K/kernel fs.img \
	mkfs/make_fs .gdbinit \
        $U/usys.S \
	$(UPROGS)

all: $K/kernel fs.img

.PHONY: clean
