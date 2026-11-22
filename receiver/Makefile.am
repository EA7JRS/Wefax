bin_PROGRAMS = wefaxreceiver
wefaxreceiver_SOURCES = main.c iq_ring.c iq_source.h rtl_source.c file_source.c sdrplay_source.c \
	kiwi_source.c kiwi_proto.c kiwi_proto.h \
	channel.c channel.h wefax_decoder.c wefax_decoder.h png_out.c png_out.h \
	store.c store.h schedule.c schedule.h navtex_share.c navtex_share.h
wefaxreceiver_LDADD = $(RECEIVER_LIBS)
wefaxreceiver_LDFLAGS = -pthread
AM_CPPFLAGS = -DDATA_DIR=\"/opt/Wefax\" -DSTATIONS_FILE=\"/opt/Wefax/build/estaciones.json\"
AM_CFLAGS = -Wall -Wextra

# make check: the decoder alone, the whole chain from receiver samples to a PNG, and the rest
check_PROGRAMS = test_decoder test_channel test_store test_kiwi
TESTS = test_decoder test_channel test_store test_kiwi
test_kiwi_SOURCES = ../test/test_kiwi.c kiwi_proto.c wefax_decoder.c channel.c
test_decoder_SOURCES = ../test/test_decoder.c wefax_decoder.c
test_channel_SOURCES = ../test/test_channel.c wefax_decoder.c channel.c png_out.c
test_store_SOURCES = ../test/test_store.c store.c schedule.c navtex_share.c
CLEANFILES = -r test_data

wefaxdir = /opt/Wefax
# The directory of the charts and the database belong to whoever ran "sudo make install"
install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(wefaxdir)/charts
	@chmod 775 $(DESTDIR)$(wefaxdir) $(DESTDIR)$(wefaxdir)/charts
	@if [ -n "$$SUDO_USER" ] && [ -z "$(DESTDIR)" ]; then chown "$$SUDO_USER" $(wefaxdir) $(wefaxdir)/charts; fi
