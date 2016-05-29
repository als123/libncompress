
PACKAGE_VERSION = 4.2
PACKAGE_RELEASE = 5
PACKAGE_TARNAME = libncompress

prefix      = /usr/local
exec_prefix = ${prefix}
incdir      = $(DEST_DIR)${prefix}/include
libdir      = $(DEST_DIR)${exec_prefix}/lib64/
bindir      = $(DEST_DIR)${exec_prefix}/bin/
mandir      = $(DEST_DIR)${prefix}/share/man/
docdir      = $(DEST_DIR)${prefix}/share/doc/${PACKAGE_TARNAME}/

# On suse, docdir will include /packages/
docsubdir   = $(docdir)/$(PACKAGE_TARNAME)

INSTALL     = install

debug   = no
profile = no

# This is for access to strdup()
STD = --std=gnu99

ifeq ($(debug),yes)
DBG_CFLAGS = -g -O0
else
DBG_CFLAGS = -O3
endif

ifeq ($(profile),yes)
PROF_FLAGS = -pg
endif

# In case the .a is linked into a .so we ensure all code is PIC.
CFLAGS = $(DBG_CFLAGS) $(PROF_FLAGS) $(STD) -fpic

LIB_MAJOR   = $(word 1,$(subst ., ,$(PACKAGE_VERSION)))
LIB_VERSION = $(PACKAGE_VERSION).$(PACKAGE_RELEASE)

LIB_SO = libncompress.so
LIB_A  = libncompress.a

all: $(LIB_SO) $(LIB_A)

install: $(LIB_SO) $(LIB_A)
	$(INSTALL) -d $(libdir) $(incdir) $(docsubdir) $(docsubdir)/tests
	$(INSTALL) ncompress42.h $(incdir)
	$(INSTALL) $(LIB_SO) $(libdir)/$(LIB_SO).$(LIB_VERSION)
	ln -s $(LIB_SO).$(LIB_VERSION) $(libdir)/$(LIB_SO).$(LIB_MAJOR)
	ln -s $(LIB_SO).$(LIB_MAJOR) $(libdir)/$(LIB_SO)
	$(INSTALL) $(LIB_A) $(libdir)
	$(INSTALL) tests/* $(docsubdir)/tests


$(LIB_SO): ncompress42.o
	$(CC) -shared -o $(LIB_SO) ncompress42.o


$(LIB_A): ncompress42.o
	$(AR) rv $(LIB_A) ncompress42.o


%.html: %.md
	pandoc --from markdown_github --to html5 -H style.css -o $@ $^

clean::
	$(RM) *.o

veryclean:: clean
	$(RM) $(LIB_A) $(LIB_SO) README.html

distclean:: veryclean
	$(RM) -f configure config.status config.log autom4te.cache/  
