sc0710-objs := \
	sc0710-cards.o sc0710-core.o sc0710-i2c.o \
	sc0710-formats.o sc0710-dma-channel.o

obj-m += sc0710.o

TARFILES = Makefile *.h *.c *.txt *.md

KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

load:	all
	sudo dmesg -c >/dev/null
	sudo cp /dev/null /var/log/debug
	sudo insmod ./sc0710.ko

unload:
	sudo rmmod sc0710
	sync

tarball:
	tar zcf ../sc0710-dev-$(shell date +%Y%m%d-%H%M%S).tgz $(TARFILES)

