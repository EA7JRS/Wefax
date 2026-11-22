bin_PROGRAMS = wefaxwebserver
wefaxwebserver_SOURCES = wefax_web.c gps.c gps.h voacap.c voacap.h ../receiver/store.c ../receiver/store.h ../receiver/schedule.c ../receiver/schedule.h
wefaxwebserver_LDADD = -lmicrohttpd -lm
wefaxwebserver_LDFLAGS = -pthread
AM_CPPFLAGS = -DDATA_DIR=\"/opt/Wefax\" -DWEB_ROOT=\"/opt/Wefax/build\"
AM_CFLAGS = -Wall -Wextra
EXTRA_DIST = build

wefaxdir = /opt/Wefax
# Replaces the installed interface instead of nesting build/build on a reinstall
install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(wefaxdir)
	@rm -rf $(DESTDIR)$(wefaxdir)/build
	@cp -r $(srcdir)/build $(DESTDIR)$(wefaxdir)/build

# make check: the VOACAP input and the reading of its output (the program is not needed)
check_PROGRAMS = test_voacap
TESTS = test_voacap
test_voacap_SOURCES = ../test/test_voacap.c voacap.c voacap.h
test_voacap_LDADD = -lm
test_voacap_LDFLAGS = -pthread
