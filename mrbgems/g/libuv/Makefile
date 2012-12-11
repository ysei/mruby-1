GEM := libuv
LIBUV := ./libuv/libuv.a

include $(MAKEFILE_4_GEM)

GEM_C_FILES := $(wildcard $(SRC_DIR)/*.c)
GEM_OBJECTS := $(patsubst %.c, %.o, $(GEM_C_FILES)) $(LIBUV)

$(LIBUV) :
	make all -C  $(MRUBY_ROOT)/mrbgems/g/libuv/libuv/
	cp $(LIBUV) $(GEM_PACKAGE)

gem-all : $(GEM_OBJECTS) gem-c-files

gem-clean : gem-clean-c-files
