APP = md009

OSDIR = mdepx

CMD = python3 -B ${OSDIR}/tools/emitter.py

all:
	@${CMD} -j mdepx.conf
	@${CROSS_COMPILE}objcopy -O ihex obj/${APP}.elf obj/${APP}.hex
	@${CROSS_COMPILE}objcopy -O binary obj/${APP}.elf obj/${APP}.bin

debug:
	@${CMD} -d mdepx.conf
	@${CROSS_COMPILE}objcopy -O ihex obj/${APP}.elf obj/${APP}.hex
	@${CROSS_COMPILE}objcopy -O binary obj/${APP}.elf obj/${APP}.bin

clean:
	@rm -rf obj/*

include ${OSDIR}/mk/user.mk
