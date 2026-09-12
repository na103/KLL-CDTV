# Klingon Language Lab for the Commodore CDTV
#
#   make ISO=path/to/klingon.iso   executable, converted data and bootable ISO
#   make check                     check tools and input files, then stop
#   make extract                   unpack the original ISO into build/src
#   make exe                       just the Amiga executable
#   make media                     convert the pictures, audio and clips
#   make test                      check the C decoder against the Python one
#
# The original CD is not part of this repository: point ISO at your own image
# (the default is iso/klingon.ISO). The same goes for CDTV.TM and RMTM, which
# belong to Commodore and go in cdtv/ (see the README).
#
# Converting the clips is the slow part (about a hundred of them):
# "make MEDIA_FLAGS=--no-video" skips them, "make MEDIA_FLAGS=--only=2"
# converts only the first two entries of each category.

PYTHON   ?= python3
CC       := m68k-amigaos-gcc
VASM     := vasmm68k_mot
GEN      := build/gen
CFLAGS   := -Os -m68000 -mcrt=nix13 -Wall -fomit-frame-pointer -I$(GEN)
# quitting reboots the CDTV; "make KLL_NO_REBOOT=1" returns to the CLI instead
CFLAGS   += $(if $(KLL_NO_REBOOT),-DKLL_NO_REBOOT)
LDFLAGS  := -mcrt=nix13 -s

# interface font: the 1.3 ROM only has topaz 8, eight pixels wide per
# character, so a proportional one of the same height is built instead
FONT_TTF  ?= /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
FONT_SIZE ?= 10

# the original CD image and the two Commodore files, all supplied by you
ISO      ?= iso/klingon.ISO
TM       ?= cdtv/CDTV.TM
RMTM     ?= cdtv/RMTM
SRC      := build/src
OBJ      := build/obj
CD       := build/cd
OUT_ISO  := build/KLL_CDTV.iso
MEDIA_FLAGS ?=

OBJS := $(addprefix $(OBJ)/,main.o app.o ui.o db.o snd.o audio.o video.o delta.o pic.o port.o vbl.o)
HOSTCC ?= cc

# screens the shared interface palette is computed from
GUI_SCREENS := $(addprefix $(SRC)/FIN_IMS2/,MAINB.BMP MAIN_01.BMP MAIN_02.BMP \
	MAIN_03.BMP MAIN_04.BMP HOLB.BMP HOL_01.BMP HOL_02.BMP HOL_03.BMP HOL_04.BMP \
	PRONUNB.BMP PRONU_01.BMP PRONU_02.BMP PRONU_03.BMP PRONU_04.BMP \
	PRONU_05.BMP SCORE.BMP NAKED.BMP SPLASH.BMP)

# full screens copied to the CD (the name on the CD is the original one)
# SPLASH01..05 (production credits and Simon & Schuster logo) are left out:
# the program starts straight at the title screen
SCREENS := MAINB MAIN_01 MAIN_02 MAIN_03 MAIN_04 \
	HOLB HOL_01 HOL_02 HOL_03 HOL_04 SPLASH SCORE \
	PRONUNB PRONU_01 PRONU_02 PRONU_03 PRONU_04 PRONU_05

DATA := $(addprefix $(CD)/DATA/,$(addsuffix .PIC,$(SCREENS))) \
	$(CD)/DATA/CREDITS.PIC $(CD)/DATA/HOLBTN.PIC $(CD)/DATA/PRONBTN.PIC \
	$(CD)/DATA/KLL.DB \
	build/stamp-mainovl build/stamp-drill

CD_FILES := $(CD)/KLL $(CD)/C/RMTM $(CD)/S/Startup-Sequence $(DATA) build/stamp-media

.PHONY: all check extract exe media test clean distclean

all: check $(OUT_ISO)

extract: check $(SRC)/.done

exe: $(CD)/KLL

media: build/stamp-media

# ------------------------------------------------------------------- checks

# Everything the build needs, reported in one go: fixing three missing things
# at once beats hitting them one at a time, halfway through a long conversion.
check:
	@$(PYTHON) tools/checkenv.py --cc $(CC) --vasm $(VASM) \
		--iso "$(ISO)" --tm "$(TM)" --rmtm "$(RMTM)" --font "$(FONT_TTF)"

$(SRC)/.done: $(ISO) tools/isoextract.py
	$(PYTHON) tools/isoextract.py $< $(SRC)
	touch $@

# ----------------------------------------------------------------- program

$(GEN)/layout.h: tools/layout.py
	@mkdir -p $(@D)
	$(PYTHON) tools/layout.py header $@

$(GEN)/font.h: tools/mkfont.py
	@mkdir -p $(@D)
	$(PYTHON) tools/mkfont.py $(FONT_TTF) $(FONT_SIZE) $@

$(OBJ)/%.o: src/%.c $(wildcard src/*.h) $(GEN)/layout.h $(GEN)/palette.h $(GEN)/font.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ)/%.o: src/%.s
	@mkdir -p $(@D)
	$(VASM) -Fhunk -m68000 -quiet -o $@ $<

$(CD)/KLL: $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^

$(CD)/S/Startup-Sequence: cdroot/S/Startup-Sequence
	@mkdir -p $(@D)
	cp $< $@

# Commodore utility that removes the CDTV trademark screen (not included)
$(CD)/C/RMTM: $(RMTM)
	@mkdir -p $(@D)
	cp $< $@

# ----------------------------------------------------------------- graphics

build/gui_palette.json $(GEN)/palette.h: tools/convimg.py tools/amigagfx.py | $(SRC)/.done
	@mkdir -p $(GEN)
	$(PYTHON) tools/convimg.py palette build/gui_palette.json \
		--header $(GEN)/palette.h $(GUI_SCREENS)

$(CD)/DATA/CREDITS.PIC: build/gui_palette.json | $(SRC)/.done
	@mkdir -p $(@D)
	$(PYTHON) tools/convimg.py pic $< $(SRC)/FIN_IMS2/TMG_CRDT.BMP $@

$(CD)/DATA/%.PIC: build/gui_palette.json | $(SRC)/.done
	@mkdir -p $(@D)
	$(PYTHON) tools/convimg.py pic $< $(SRC)/FIN_IMS2/$*.BMP $@

# lit and unlit cutouts of the HOL screen commands, in a single image
$(CD)/DATA/HOLBTN.PIC: build/gui_palette.json tools/layout.py | $(SRC)/.done
	@mkdir -p $(@D)
	$(PYTHON) tools/convimg.py atlas $< $(SRC)/FIN_IMS2 $@

# commands and the 34 phoneme buttons of the pronunciation screen
$(CD)/DATA/PRONBTN.PIC: build/gui_palette.json tools/layout.py | $(SRC)/.done
	@mkdir -p $(@D)
	$(PYTHON) tools/convimg.py atlas $< $(SRC)/FIN_IMS2 $@ --which pron

# menu panels with one entry lit, one per category
build/stamp-mainovl: build/gui_palette.json tools/layout.py tools/convimg.py | $(SRC)/.done
	@mkdir -p $(CD)/DATA
	$(PYTHON) tools/convimg.py mainovl build/gui_palette.json $(SRC)/FIN_IMS2 $(CD)/DATA
	touch $@

# drill screens: four per category plus the lit cutouts
build/stamp-drill: build/gui_palette.json tools/layout.py tools/convimg.py | $(SRC)/.done
	@mkdir -p $(CD)/DRILL
	$(PYTHON) tools/convimg.py drill build/gui_palette.json $(SRC)/FIN_IMS2 $(CD)/DRILL
	touch $@

# ------------------------------------------------------------------ content

$(CD)/DATA/KLL.DB: tools/mkdb.py tools/layout.py | $(SRC)/.done
	@mkdir -p $(@D)
	$(PYTHON) tools/mkdb.py $(SRC)/DATAFILE $@

# pictures, audio and clips of the entries: the conversion picks up where it
# left off, so only the tools themselves affect the timestamp
build/stamp-media: tools/mkmedia.py tools/mkdb.py tools/layout.py tools/convimg.py \
		tools/avi2kxl.py tools/kxl.py tools/amigagfx.py build/gui_palette.json \
		Makefile | $(SRC)/.done
	$(PYTHON) tools/mkmedia.py $(SRC) $(CD) --palette build/gui_palette.json $(MEDIA_FLAGS)
	touch $@

$(OUT_ISO): $(CD_FILES) tools/mkcdtv.py
	$(PYTHON) tools/mkcdtv.py $(CD) $@ --tm $(TM) --volume KLL

# -------------------------------------------------------------------- tests

build/delta_test: tests/delta_test.c src/delta.c src/delta.h tests/host/exec/types.h
	$(HOSTCC) -O2 -Wall -Itests/host -Isrc -o $@ tests/delta_test.c src/delta.c

# the same test built for the Amiga, run under vamos when it is available
build/delta_test.amiga: tests/delta_test.c src/delta.c src/delta.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/delta_test.c src/delta.c

test: build/delta_test build/delta_test.amiga build/stamp-media
	for f in $(CD)/VIDEO/U001.KXL $(CD)/VIDEO/M001.KXL; do \
		test -f $$f || continue; \
		build/delta_test $$f build/delta_test.raw && \
		$(PYTHON) tools/kxlcheck.py $$f --raw build/delta_test.raw || exit 1; \
		if command -v vamos >/dev/null; then \
			(cd build && vamos delta_test.amiga $${f#build/} delta_test_amiga.raw) && \
			$(PYTHON) tools/kxlcheck.py $$f --raw build/delta_test_amiga.raw || exit 1; \
		fi; \
	done

clean:
	rm -rf $(OBJ) $(CD) $(GEN) $(OUT_ISO) build/gui_palette.json build/stamp-* \
		build/delta_test build/delta_test.raw \
		build/delta_test.amiga build/delta_test_amiga.raw

distclean:
	rm -rf build
