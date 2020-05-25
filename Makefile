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

dtb:
	cpp -nostdinc -Imdepx/dts -Imdepx/dts/arm -Imdepx/dts/common	\
	    -Imdepx/dts/include -undef, -x assembler-with-cpp md009.dts	\
	    -O obj/md009.dts
	dtc -I dts -O dtb obj/md009.dts -o obj/md009.dtb
	bin2hex.py --offset=1015808 obj/md009.dtb obj/md009.dtb.hex
	nrfjprog -f NRF91 --erasepage 0xf8000-0xfc000
	nrfjprog -f NRF91 --program obj/md009.dtb.hex -r

flash:
	nrfjprog -f NRF91 --erasepage 0x40000-0xf8000
	nrfjprog -f NRF91 --program obj/md009.hex -r

reset:
	nrfjprog -f NRF91 -r

clean:
	@rm -rf obj/*

include ${OSDIR}/mk/user.mk
