# dwm - dynamic window manager
# See LICENSE file for copyright and license details.

include config.mk

SRC = drw.c dwm.c util.c layouts.c
ADDSRC = util.c
HDR = config.h drw.h dwm.h layouts.h util.h sockdef.h
OBJ = ${SRC:.c=.o}

all: dwm dwmq

options:
	@echo dwm build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: ${HDR} ${ADDSRC} config.mk

dwm: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

dwmq: dwmq.c sockdef.h
	${CC} -o $@ $<

clean:
	rm -f dwm ${OBJ}

install: all dwm.1
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f dwm dwmc dwmq ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/dwm
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	sed "s/VERSION/${VERSION}/g" < dwm.1 > ${DESTDIR}${MANPREFIX}/man1/dwm.1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/dwm.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/dwm\
		${DESTDIR}${MANPREFIX}/man1/dwm.1

.PHONY: all options clean install uninstall
