include m4/aminclude.am
AM_CPPFLAGS = -Wall -Wextra -Wno-long-long -ansi -pedantic -std=gnu++0x

sbin_PROGRAMS = fatx
dist_man_MANS = debian/fatx.8 debian/fsck.fatx.8 debian/fusefatx.8 debian/label.fatx.8 debian/mkfs.fatx.8 debian/unrm.fatx.8
dist_doc_DATA = README BUGS
EXTRA_DIST = test.sh doxygen.cfg xbox.cpp include

if xbe
noinst_DATA = fsck.xbe unrm.xbe
noinst_PROGRAMS = fsck unrm
MINGW32CPP = i686-pc-mingw32-g++
endif

fatx_SOURCES = fatx.cpp fatx.hpp config.h Makefile.am
fatx_LDADD = -lboost_program_options
fatx_CPPFLAGS = $(AM_CPPFLAGS)
if fuse
fatx_LDADD += $(fuse_LIBS)
fatx_CPPFLAGS += $(fuse_CFLAGS)
endif
fatx_CPPFLAGS += -D NO_SPLICE
#fatx_CPPFLAGS += -D DBGCOLOR=-1
#fatx_CPPFLAGS += -D DBG_INIT
#fatx_CPPFLAGS += -D DBG_READ
#fatx_CPPFLAGS += -D DBG_WRITE
#fatx_CPPFLAGS += -D DBG_SEM
#fatx_CPPFLAGS += -D DBGSEM=\"FAT\"
#fatx_CPPFLAGS += -D DBG_BUFFER
#fatx_CPPFLAGS += -D DBGBUFDMP=256
#fatx_CPPFLAGS += -D DBG_CACHE
#fatx_CPPFLAGS += -D DBG_CACHDMP
#fatx_CPPFLAGS += -D DBG_AREAS
#fatx_CPPFLAGS += -D DBG_GUESS
#fatx_CPPFLAGS += -D DBGCR=32
#fatx_CPPFLAGS += -D DBGLIMIT=64
#fatx_CPPFLAGS += -D DBG_FAT
#fatx_CPPFLAGS += -D DBG_GAPS
#fatx_CPPFLAGS += -D NO_WRITE
#fatx_CPPFLAGS += -D NO_FUSE
#fatx_CPPFLAGS += -D NO_LOCK
#fatx_CPPFLAGS += -D NO_CACHE
#fatx_CPPFLAGS += -D NO_OPTION

if xbe
XBOXPREFIX = /usr/local/openxdk
XBOXCPPFLAGS = -march=i386
XBOXCPPFLAGS += -nostdlib
XBOXCPPFLAGS += -fno-builtin
XBOXCPPFLAGS += -fno-exceptions
XBOXCPPFLAGS += -I$(XBOXPREFIX)/i386-pc-xbox/include
XBOXCPPFLAGS += -I$(XBOXPREFIX)/include
#XBOXCPPFLAGS += -I$(XBOXPREFIX)/include/SDL
XBOXCPPFLAGS += -D ENABLE_XBOX
XBOXCPPFLAGS += -D DISABLE_CDROM
XBOXLDFLAGS = -shared
XBOXLDFLAGS += -nostdlib
XBOXLDFLAGS += -Wl,--file-alignment,0x20
XBOXLDFLAGS += -Wl,--section-alignment,0x20
XBOXLDFLAGS += -Wl,--entry,_WinMainCRTStartup
XBOXLDFLAGS += -Wl,--strip-all
XBOXLDFLAGS += -L$(XBOXPREFIX)/lib
XBOXLDFLAGS += -L$(XBOXPREFIX)/i386-pc-xbox/lib
XBOXLDADD = -lopenxdk -lhal -lc -lusb -lhal -lc -lxboxkrnl -lstdc++

fsck_SOURCES = fsck.cpp fatx.cpp fatx.hpp config.h Makefile.am
fsck_CPPFLAGS = $(AM_CPPFLAGS) $(XBOXCPPFLAGS) -D XBOX_FSCK
fsck_LDFLAGS = $(XBOXLDFLAGS)
fsck_LDADD = $(XBOXLDADD)
fsck_LINK = $(MINGW32CPP) -o $@ $(XBOXLDFLAGS)

unrm_SOURCES = unrm.cpp fatx.cpp fatx.hpp config.h Makefile.am
unrm_CPPFLAGS = $(AM_CPPFLAGS) $(XBOXCPPFLAGS) -D XBOX_UNRM
unrm_LDFLAGS = $(XBOXLDFLAGS)
unrm_LDADD = $(XBOXLDADD)
unrm_LINK = $(MINGW32CPP) -o $@ $(XBOXLDFLAGS)
endif

CXBE = cxbe
SUFFIXES = .xbe
.cpp.xbe:
	test -x $*$(EXEEXT) || make $*$(EXEEXT)
	$(CXBE) -MODE:retail -TITLE:$@ -OUT:$@ $*$(EXEEXT) >/dev/null

fsck.xbe: fsck$(EXEEXT)
unrm.xbe: unrm$(EXEEXT)

fsck.cpp: xbox.cpp
	$(LN_S) $< $@
unrm.cpp: xbox.cpp
	$(LN_S) $< $@

TESTS =
TESTS += test1
if fuse
TESTS += test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12 test13 test14 test15 test16 test17 test18 test19 test20 test21 test22
endif
TESTS += test23
TESTS += test0

test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12 test13 test14 test15 test16 test17 test18 test19 test20 test21 test22 test23 test0 : test.sh
	$(LN_S) $< $@

doc:
	make doxygen-doc

clean-local:
	-$(RM) -r mnt
maintainer-clean-local:
	-$(RM) -r debian/fatx debian/patches doc autom4te.cache .deps .pc $(PACKAGE_NAME)-$(PACKAGE_VERSION)

CLEANFILES =
CLEANFILES += *~ *.log debian/*.log test[0-9]* unrm.cpp fsck.cpp $(EXTRA_PROGRAMS) disk.fatx *fatx name.txt fuse[0-9]* tbff* FILE* fsck$(EXEEXT) unrm$(EXEEXT)
DISTCLEANFILES =
DISTCLEANFILES += config.h config.cache config.log config.status stamp-h1 compile missing
MAINTAINERCLEANFILES =
MAINTAINERCLEANFILES += analyse.sh profile.sh fsck.xbe unrm.xbe  $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
MAINTAINERCLEANFILES += aclocal.m4 configure depcomp config.h.in install-sh test-driver Makefile.in

install-exec-hook:
	cd $(DESTDIR)$(sbindir) && \
	$(LN_S) fatx$(EXEEXT) fusefatx && \
	$(LN_S) fatx$(EXEEXT) mkfs.fatx && \
	$(LN_S) fatx$(EXEEXT) fsck.fatx && \
	$(LN_S) fatx$(EXEEXT) unrm.fatx && \
	$(LN_S) fatx$(EXEEXT) label.fatx

uninstall-hook:
	cd $(DESTDIR)$(sbindir) && \
	$(RM) fusefatx mkfs.fatx fsck.fatx unrm.fatx label.fatx
