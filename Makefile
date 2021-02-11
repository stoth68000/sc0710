sc0710-objs := \
	sc0710-cards.o sc0710-core.o sc0710-i2c.o \
	sc0710-formats.o sc0710-dma-channel.o sc0710-dma-channels.o \
	sc0710-things-per-second.o sc0710-video.o

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
	sudo modprobe videobuf2-core
	sudo modprobe videodev
	#sudo modprobe videobuf-dma-sg
	sudo modprobe videobuf-vmalloc
	sudo insmod ./sc0710.ko \
		thread_dma_poll_interval_ms=2 \
		dma_status=0

unload:
	sudo rmmod sc0710
	sync

tarball:
	tar zcf ../sc0710-dev-$(shell date +%Y%m%d-%H%M%S).tgz $(TARFILES)

deps:
	sudo yum -y install v4l-utils
