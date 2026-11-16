# mupf: lhde's document viewer (docs/USAGE.md "mupf"): PDF, XPS, EPUB and the other formats of mupdf, drawn by mupdf on a
# thread of its own (engine_native.cpp, the one file that includes mupdf's headers); its layout and render cache
# (layout_native.cpp) are plain C++ that tests/toolkit/mupftest.cpp drives. File > Open is the Open dialog, so the file
# dialog's objects. Installed, with mupf.desktop (the types mupdf opens: Open With, the document viewer's role).
#
# mupdf (1.2x, the fz_* API) is found, first that answers:
#   MUPDF_DIR=DIR        a built source checkout (make in DIR: DIR/include, DIR/build/release/libmupdf.a and
#                        libmupdf-third.a, or DIR/build/shared-release/libmupdf.so); the explicit choice comes first
#   pkg-config mupdf     its Cflags and Libs; a static libmupdf.a beside it (no libmupdf.so) also takes libmupdf-third
#                        when it is there and the libraries mupdf's third-party bundle leaves out (libjpeg, libm: Void's
#                        mupdf-devel, whose .pc has no Libs.private)
#   the headers          /usr/include/mupdf or /usr/local/include/mupdf (FreeBSD's port, which has no .pc): -lmupdf from
#                        the prefix's lib, with libmupdf-third and libjpeg and libm the same way when it is static
# Without any (or with MUPDF=no), mupf is not built: make says so in one line, and its tests skip.
MUPDF_DIR ?=
MUPDF ?=
mupdf_static_extra = $(shell pkg-config --libs libjpeg 2>/dev/null || echo -ljpeg) -lm
# mupdf_libs LIBDIR: -lmupdf, with what a static one needs
mupdf_libs = -L$(1) -lmupdf $(if $(wildcard $(1)/libmupdf.so $(1)/libmupdf.so.*),,$(if $(wildcard $(1)/libmupdf-third.*),-lmupdf-third) $(mupdf_static_extra))
ifeq ($(MUPDF),no)
  MUPDF_FOUND :=
else ifneq ($(MUPDF_DIR),)
  MUPDF_FOUND := $(MUPDF_DIR)
  MUPDF_CFLAGS := -I$(MUPDF_DIR)/include
  ifneq ($(wildcard $(MUPDF_DIR)/build/release/libmupdf.a),)
    MUPDF_LIBS := $(MUPDF_DIR)/build/release/libmupdf.a $(wildcard $(MUPDF_DIR)/build/release/libmupdf-third.a) -lm
  else
    MUPDF_LIBS := -L$(MUPDF_DIR)/build/shared-release -lmupdf -Wl,-rpath,$(abspath $(MUPDF_DIR)/build/shared-release)
  endif
else ifeq ($(shell pkg-config --exists mupdf 2>/dev/null && echo y),y)
  MUPDF_FOUND := pkg-config
  MUPDF_CFLAGS := $(shell pkg-config --cflags mupdf)
  MUPDF_LIBDIR := $(shell pkg-config --variable=libdir mupdf)
  MUPDF_LIBS := $(shell pkg-config --libs mupdf) $(if $(wildcard $(MUPDF_LIBDIR)/libmupdf.so $(MUPDF_LIBDIR)/libmupdf.so.*),,$(if $(wildcard $(MUPDF_LIBDIR)/libmupdf-third.*),-lmupdf-third) $(mupdf_static_extra))
else ifneq ($(wildcard /usr/include/mupdf/fitz.h),)
  MUPDF_FOUND := /usr
  MUPDF_CFLAGS :=
  MUPDF_LIBS := $(call mupdf_libs,/usr/lib)
else ifneq ($(wildcard /usr/local/include/mupdf/fitz.h),)
  MUPDF_FOUND := /usr/local
  MUPDF_CFLAGS := -I/usr/local/include
  MUPDF_LIBS := $(call mupdf_libs,/usr/local/lib)
endif

MUPF_OBJS = $(call app_objs,mupf)
$(O)/apps/mupf/engine_native.o: CXXFLAGS += $(MUPDF_CFLAGS)
$(O)/apps/mupf/mupf.o: apps/mupf/mupf.res
apps/mupf/mupf.res: apps/mupf/resource.h

ifneq ($(MUPDF_FOUND),)
build/bin/mupf: $(MUPF_OBJS) $(WUX_APP_DEPS) $(WUX_APP_FILEDIALOG)
	$(app_link) $(MUPDF_LIBS)
APPS += mupf
apps programs: build/bin/mupf

install-apps: install-mupf
uninstall-apps: uninstall-mupf
install-mupf:
	install -d $(DESTDIR)$(APPDIR)
	sed 's|@BINDIR@|$(BINDIR)|g' apps/mupf/mupf.desktop.in > $(DESTDIR)$(APPDIR)/mupf.desktop
uninstall-mupf:
	rm -f $(DESTDIR)$(APPDIR)/mupf.desktop
.PHONY: install-mupf uninstall-mupf
else
apps: mupf-not-built
mupf-not-built:
	@echo "mupf not built: no mupdf (pkg-config mupdf, /usr/include/mupdf, /usr/local/include/mupdf; or make MUPDF_DIR=a built mupdf checkout)"
.PHONY: mupf-not-built
endif
