CC=cc
CFLAGS=-I. -Wall -Wextra -pedantic
LDFLAGS=

BUILDDIR = build
EXECUTABLE = $(BUILDDIR)/barmaid

# "make SYS_ZLIB=1" to use system zlib
ifdef SYS_ZLIB
LDFLAGS += -lz
else
ZLIB_VER = zlib-1.2.8
CFLAGS += -I$(ZLIB_VER)
CSRC += $(ZLIB_VER)/inflate.c $(ZLIB_VER)/zutil.c $(ZLIB_VER)/adler32.c \
        $(ZLIB_VER)/crc32.c $(ZLIB_VER)/inffast.c $(ZLIB_VER)/inftrees.c
endif

CSRC += barmaid.c barflate.c
COBJ += $(patsubst %, $(BUILDDIR)/%,$(CSRC:.c=.o))

all: $(EXECUTABLE) $(BUILDDIR)/.sentinel

$(COBJ) : $(BUILDDIR)/%.o : %.c $(BUILDDIR)/.sentinel
	@echo COMPILING: $<
	$(CC) -c $(CFLAGS) $< -o $@

$(EXECUTABLE):  $(COBJ)
	@echo LINKING: $@
	$(CC) $(COBJ) $(LDFLAGS) -o $@

.PRECIOUS: %/.sentinel
%/.sentinel:
	@mkdir -p ${@D}
	@mkdir -p $(BUILDDIR)/$(ZLIB_VER)
	@touch $@

clean:
	@echo CLEANING UP:
	rm -rf $(BUILDDIR)
