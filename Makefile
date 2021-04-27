

#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
#
# NO_SMDH: if set to anything, no SMDH file is generated.
# ROMFS is the directory which contains the RomFS, relative to the Makefile (Optional)
# APP_TITLE is the name of the app stored in the SMDH file (Optional)
# APP_DESCRIPTION is the description of the app stored in the SMDH file (Optional)
# APP_AUTHOR is the author of the app stored in the SMDH file (Optional)
# ICON is the filename of the icon (.png), relative to the project folder.
#   If not set, it attempts to use one of the following (in this order):
#     - <Project name>.png
#     - icon.png
#     - <libctru folder>/default_icon.png
#---------------------------------------------------------------------------------
APP_TITLE	:=	SNES9x for 3DS
APP_DESCRIPTION	:=	SNES emulator for 3DS.
APP_AUTHOR	:=	bubble2k16
ASSETS		:=	assets
ICON		:=	$(ASSETS)/icon.png

TARGET		:=	snes9x_3ds
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------


ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -w -O3 -mword-relocations -finline-limit=20000 \
			-fomit-frame-pointer -ffunction-sections \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM11 -D_3DS

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

#CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
#CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES		:=
CPPFILES	:=	3dsmain.cpp 3dsmenu.cpp 3dsopt.cpp \
			3dsgpu.cpp 3dssound.cpp 3dsui.cpp 3dsexit.cpp \
			3dsconfig.cpp 3dsfiles.cpp 3dsinput.cpp 3dsmatrix.cpp \
			3dsimpl.cpp 3dsimpl_tilecache.cpp 3dsimpl_gpu.cpp \
			gpulib.cpp lodepng.cpp \
			Snes9x/bsx.cpp Snes9x/fxinst.cpp Snes9x/fxemu.cpp Snes9x/fxdbg.cpp Snes9x/c4.cpp Snes9x/c4emu.cpp \
			Snes9x/soundux.cpp Snes9x/spc700.cpp Snes9x/apu.cpp Snes9x/cpuexec.cpp Snes9x/sa1cpu.cpp Snes9x/hwregisters.cpp \
			Snes9x/cheats.cpp Snes9x/cheats2.cpp Snes9x/sdd1emu.cpp Snes9x/spc7110.cpp Snes9x/obc1.cpp \
			Snes9x/seta.cpp Snes9x/seta010.cpp Snes9x/seta011.cpp Snes9x/seta018.cpp \
			Snes9x/snapshot.cpp Snes9x/dsp.cpp Snes9x/dsp1.cpp Snes9x/dsp2.cpp Snes9x/dsp3.cpp Snes9x/dsp4.cpp \
			Snes9x/cpu.cpp Snes9x/sa1.cpp Snes9x/debug.cpp Snes9x/apudebug.cpp Snes9x/sdd1.cpp Snes9x/tile.cpp Snes9x/srtc.cpp \
			Snes9x/gfx.cpp Snes9x/gfxhw.cpp Snes9x/memmap.cpp Snes9x/cliphw.cpp \
			Snes9x/ppu.cpp Snes9x/ppuvsect.cpp Snes9x/dma.cpp Snes9x/data.cpp Snes9x/globals.cpp \
			
SFILES        :=    $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
#---------------------------------------------------------------------------------
	export LD	:=	$(CC)
#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
	export LD	:=	$(CXX)
#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
			$(PICAFILES:.v.pica=.shbin.o) \
			$(SHLISTFILES:.shlist=.shbin.o) \
			$(CPPFILES:.cpp=.o) \
			$(CFILES:.c=.o) \
			$(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD) \
			-I$(CURDIR)/$(BUILD)/Snes9x \
			-I$(CURDIR)/$(SOURCES) \
			-I$(CURDIR)/$(SOURCES)/Snes9x \

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

ifneq ($(ROMFS),)
	export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

#---------------------------------------------------------------------------------
# OS detection to automatically determine the correct makerom variant to use for
# CIA creation
#---------------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
	MAKEROM := ./makerom/windows_x86_64/makerom.exe
else
	ifneq ($(shell command -v makerom),)
		MAKEROM := makerom
	else
		UNAME_S := $(shell uname -s)
		ifeq ($(UNAME_S),Linux)
			MAKEROM := ./makerom/linux_x86_64/makerom
		endif
		ifeq ($(UNAME_S),Darwin)
			MAKEROM := ./makerom/darwin_x86_64/makerom
		endif
	endif
endif
#---------------------------------------------------------------------------------


#---------------------------------------------------------------------------------
# deploy via 3DS LINK
#----------------------------
DEPLOY := $(DEVKITARM)/bin/3dslink.exe
#---------------------------------------------------------------------------------


.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD) cia

$(BUILD):
	@[ -d $@ ] || mkdir -p $@    
	@mkdir -p $(BUILD)/Snes9x
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
cia: $(BUILD)
ifneq ($(MAKEROM),)
	$(MAKEROM) -rsf $(OUTPUT).rsf -elf $(OUTPUT).elf -icon $(OUTPUT).icn -banner $(OUTPUT).bnr -f cia -o $(OUTPUT).cia
else
	$(error "CIA creation is not supported on this platform ($(UNAME_S)_$(UNAME_M))")
endif


#---------------------------------------------------------------------------------
deploy: $(BUILD)
	$(DEPLOY) $(OUTPUT).3dsx -0 sdmc:/3ds/$(OUTPUT).3dsx

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf


#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
ifeq ($(strip $(NO_SMDH)),)
$(OUTPUT).3dsx	:	$(OUTPUT).elf $(OUTPUT).smdh
else
$(OUTPUT).3dsx	:	$(OUTPUT).elf
endif

$(OUTPUT).elf	:	$(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)
#---------------------------------------------------------------------------------
%.png.o	:	%.png
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $(patsubst %.shbin.o,%.shbin,$(notdir $@)))
	picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $@
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"_end[];" > `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u8" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`"[];" >> `(echo $(CURBIN) | tr . _)`.h
	echo "extern const u32" `(echo $(CURBIN) | sed -e 's/^\([0-9]\)/_\1/' | tr . _)`_size";" >> `(echo $(CURBIN) | tr . _)`.h
endef

%.shbin.o : %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o : %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o : %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)/$(file)))

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
