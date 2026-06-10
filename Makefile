CC			= gcc
CFLAGS		 	= -g

SRCDIR			= src
OBJDIR			= obj

SOURCE			= $(shell find -type f -regextype posix-extended -regex ".*.(c)")
OBJECTS			= $(patsubst ${SRCDIR}/%.s,${OBJDIR}/%.o,${SOURCE})
TARGET			= proc-monitor

all: ${TARGET}

${TARGET}: ${OBJECTS}
	${CC} ${CFLAGS} $? -o $@

${OBJDIR}/%.o: ${SRCDIR}/%.c
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm -rf ${TARGET} ${OBJDIR}

test:
	sudo ./${TARGET}
