#----------------------------------------------------------------------------------
# Atari ST emulator for Atari 68030
# Example project for Hyper68k
#
# (c)2023 Anders Granlund
#----------------------------------------------------------------------------------

GCC_TARGET = m68k-atari-mint-gcc
GCC_NATIVE = gcc
STRIPX = ext/stripx

PARCP_APP = ./tools/parcp421/apple/parcp
PARCP_DST = u:/h

LIBCMINI_DIR = ext/libcmini
LIBCMINI_CRT = $(LIBCMINI_DIR)/build/crt0.o


# target parameter
ifeq ($(target),)
TARGET = release
else
TARGET = $(target)
endif

ifeq ($(TARGET),debug)
TARGET_SUFFIX = D
TARGET_DEF = -g -Os
STRIP =
else
TARGET_SUFFIX =
TARGET_DEF = -Os
STRIP = $(STRIPX) -s -v -f $(BIN)
endif

CPU = 68030

OUT_APP = hyper68k.tos
OUT_DIR = ../bin/
OBJ_DIR = obj/$(CPU)$(TARGET_SUFFIX)
DIST_NAME = Hyper68k
DIST_ZIP = hyper68k.zip
DIST_DIR = dist/$(DIST_NAME)

APP = $(OUT_DIR)$(OUT_APP)
BIN = $(OBJ_DIR)/$(OUT_APP)

DEFS = -DATARI -DCPU=$(CPU) $(TARGET_DEF)
INCLUDE  = -I$(LIBCMINI_DIR)/include -I.
LDFLAGS  = -L$(LIBCMINI_DIR)/build/ -lcmini
SFLAGS = $(DEFS) $(INCLUDE)

CFLAGS = $(DEFS) $(INCLUDE) \
	-ffast-math -nodefaultlibs -nostdlib \
	-fomit-frame-pointer -fno-exceptions \
	-Wall -Wno-multichar \
	-Wl,-Map,mapfile \
	-Wl,--traditional-format \

SRCS = \
	ext/lgcc.s \
	h68k/h68k.c \
	h68k/mmu.c \
	h68k/vec.c \
	h68k/xvec.S \
	h68k/xberr.S \
	h68k/xpviol.S \
	common.c \
	main.c
	
DEPS = Makefile common.h h68k/h68k.h

DISASM =

## Rules
.PHONY: clean cleanTarget info

target: info $(BIN)
	cp $(BIN) $(APP)
	@echo

debug:
	@make cpu=$(CPU) target=debug

release:
	@make cpu=$(CPU) target=release

info:
	@echo Building $(APP) \($(CPU) : $(TARGET)\)

$(OBJ_DIR):
	@[ -d obj ] || mkdir obj > /dev/null 2>&1
	@[ -d $(OBJ_DIR) ] || mkdir $(OBJ_DIR) > /dev/null 2>&1

define SRCS_LIST_TO_OBJS
	$(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(foreach file, $(SRCS), \
	$(basename $(notdir $(file))))))
endef
OBJS = $(SRCS_LIST_TO_OBJS)

SRC_PATHS += $(sort $(foreach file, $(SRCS), $(dir $(file))))
VPATH :=
VPATH += $(addprefix :, $(subst  ,:, $(filter-out $($(subst, :, ,$(VPATH))), $(SRC_PATHS))))

$(BIN): $(OBJ_DIR) $(DISASM) $(STRIPX) $(LIBCMINI_CRT) $(GEMLIB) $(OBJS)
	$(GCC_TARGET) $(CFLAGS) $(LIBCMINI_CRT) $(OBJS) $(LDFLAGS) -o $(BIN)
	$(STRIP)

$(LIBCMINI_CRT):
	cd $(LIBCMINI_DIR) && make

$(STRIPX): $(STRIPX).c
	$(GCC_NATIVE) $(STRIPX).c -o $(STRIPX)

$(OBJ_DIR)/%.o : %.S $(DEPS)
	$(GCC_TARGET) $(SFLAGS) -m$(CPU) -c $< -o $@

$(OBJ_DIR)/%.o : %.c $(DEPS)
	$(GCC_TARGET) $(CFLAGS) -m$(CPU) -c $< -o $@

%.disasm : %.c $(DEPS)
	$(GCC_TARGET) $(CFLAGS) -m$(CPU) -fverbose-asm -g -S $< -o $@

disasm: $(DISASM)

clean:
	@rm -f *~ *.bak *.disasm obj.0000.* parcp.cmd mapfile
	@make cleanTarget target=debug
	@make cleanTarget target=release
	@rm -f $(APP)

cleanTarget:
	@rm -f $(BIN)
	@rm -f $(OBJ_DIR)/*
	@rm -f $(OBJ_DIR)/.DS_Store
	@rm -f -d $(OBJ_DIR)


go:
	echo "CD $(PARCP_DST)" > parcp.cmd
	echo "LCD $(OUT_DIR)" >> parcp.cmd
	echo "PUT $(OUT_APP)" >> parcp.cmd
#	echo "EXEC -n $(OUT_APP)" >> parcp.cmd
	echo "QUIT" >> parcp.cmd
	$(PARCP_APP) -b parcp.cmd

