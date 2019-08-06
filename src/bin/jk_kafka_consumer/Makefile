# src/bin/jk_kafka_orchestrator/Makefile

PGFILEDESC = "jk_kafka_orchestrator - decode WAL"
PGAPPICON=win32

subdir = src/bin/jk_kafka_orchestrator
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

OBJS = jk_kafka_orchestrator.o compat.o xlogreader.o rmgrdesc.o \
	$(RMGRDESCOBJS) $(WIN32RES)

OBJS += kafka_pipe_consumer.o

override CPPFLAGS := -DFRONTEND $(CPPFLAGS)
LDFLAGS += -lrdkafka

RMGRDESCSOURCES = $(sort $(notdir $(wildcard $(top_srcdir)/src/backend/access/rmgrdesc/*desc.c)))
RMGRDESCOBJS = $(patsubst %.c,%.o,$(RMGRDESCSOURCES))

all: jk_kafka_orchestrator

jk_kafka_orchestrator: $(OBJS) | submake-libpgport
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

xlogreader.c: % : $(top_srcdir)/src/backend/access/transam/%
	rm -f $@ && $(LN_S) $< .

$(RMGRDESCSOURCES): % : $(top_srcdir)/src/backend/access/rmgrdesc/%
	rm -f $@ && $(LN_S) $< .

install: all installdirs
	$(INSTALL_PROGRAM) jk_kafka_orchestrator$(X) '$(DESTDIR)$(bindir)/jk_kafka_orchestrator$(X)'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

uninstall:
	rm -f '$(DESTDIR)$(bindir)/jk_kafka_orchestrator$(X)'

clean distclean maintainer-clean:
	rm -f jk_kafka_orchestrator$(X) $(OBJS) $(RMGRDESCSOURCES) xlogreader.c
