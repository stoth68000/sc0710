sc0710-objs := \
	sc0710-cards.o sc0710-core.o sc0710-i2c.o \
	sc0710-dma-channel.o sc0710-dma-channels.o \
	sc0710-dma-chains.o sc0710-dma-chain.o \
	sc0710-things-per-second.o sc0710-video.o \
	sc0710-audio.o

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

test:
	dd if=/dev/video0 of=frame.bin bs=1843200 count=20

encode:
	#ffmpeg -f rawvideo -pixel_format uyvy422 -video_size 1280x720 -i /dev/video0 -vcodec libx264 -f mpegts encoder2.ts
	#ffmpeg -f rawvideo -pixel_format yuyv422 -video_size 1280x720 -i /dev/video0 -vcodec libx264 -f mpegts encoder3.ts
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuyv422 -video_size 1280x720 -i /dev/video0 -vcodec libx264 -f mpegts encoder0.ts

stream720p:
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuyv422 -video_size 1280x720 -i /dev/video0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316

stream720pAudio:
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuyv422 -video_size 1280x720 -i /dev/video0 \
		-f alsa -ac 2 -ar 48000 -i hw:2,0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-acodec mp2 \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316


stream720p10:
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuv422p10le -video_size 1280x720 -i /dev/video0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316

stream1080p:
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuyv422 -video_size 1920x1080 -i /dev/video0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316

stream1080pAudio:
	ffmpeg -r 59.94 -f rawvideo -pixel_format yuyv422 -video_size 1920x1080 -i /dev/video0 \
		-f alsa -ac 2 -ar 48000 -i hw:2,0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-acodec mp2 \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316

stream2160p:
	ffmpeg -r 30 -f rawvideo -pixel_format yuyv422 -video_size 3840x2160 -i /dev/video0 \
		-vcodec libx264 -preset ultrafast -tune zerolatency \
		-f mpegts udp://192.168.0.66:4001?pkt_size=1316

dumpaudioparams:
	arecord --dump-hw-params -D hw:2,0

dvtimings:
	v4l2-ctl --get-dv-timings

10bitAVC:
	./ffmpeg -y -r 59.94 -f rawvideo -pixel_format yuv422p10le -video_size 1920x1080 -i /dev/video0 \
		-vcodec libx264 -pix_fmt yuv420p10le -preset ultrafast -tune zerolatency \
		-f mpegts recording.ts

10bitHEVC:
	./ffmpeg-hevc -y -r 59.94 -f rawvideo -pixel_format yuv422p10le -video_size 1920x1080 -i /dev/video0 \
		-vcodec libx265 -pix_fmt yuv422p10le -preset ultrafast -tune zerolatency \
		-f mpegts recording.ts


probe:
	# See https://codecalamity.com/encoding-uhd-4k-hdr10-videos-with-ffmpeg/
	./ffprobe-hevc -hide_banner -loglevel warning -select_streams v -print_format json -show_frames \
		-read_intervals "%+#1" -show_entries "frame=color_space,color_primaries,color_transfer,side_data_list,pix_fmt" -i recording.ts 

#yuv422p10le 10bit 4:2:2
