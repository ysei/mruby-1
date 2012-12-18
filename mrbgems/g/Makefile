ifeq ($(strip $(MRUBY_ROOT)),)
  MRUBY_ROOT := $(realpath ../../..)
endif

MAKEFILE_4_GEM := $(MRUBY_ROOT)/mrbgems/Makefile4gem

INCLUDE := 
CFLAGS := -I. -I$(MRUBY_ROOT)/include -I$(MRUBY_ROOT)/src $(INCLUDE)

ifeq ($(OS),Windows_NT)
  MAKE_FLAGS = --no-print-directory CC=$(CC) LL=$(LL) LDFLAGS='$(LDFLAGS)' CFLAGS='$(CFLAGS)' MRUBY_ROOT='$(MRUBY_ROOT)' MAKEFILE_4_GEM='$(MAKEFILE_4_GEM)'
else
  MAKE_FLAGS = --no-print-directory CC='$(CC)' LL='$(LL)' LDFLAGS='$(LDFLAGS)' CFLAGS='$(CFLAGS)' MRUBY_ROOT='$(MRUBY_ROOT)' MAKEFILE_4_GEM='$(MAKEFILE_4_GEM)'
endif

.PHONY : all
all : all_gems

all_gems :
	$(MAKE) -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/bignum $(MAKE_FLAGS)
	$(MAKE) -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/require $(MAKE_FLAGS)
	$(MAKE) -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example $(MAKE_FLAGS)


.PHONY : prepare-test
prepare-test :
	cat /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/bignum/test/*.rb  /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/require/test/*.rb  /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example/test/*.rb  > mrbgemtest.rbtmp
	$(MRUBY_ROOT)/bin/mrbc -Bmrbgemtest_irep -omrbgemtest.ctmp mrbgemtest.rbtmp

.PHONY : clean
clean :
	$(RM) *.c *.d *.rbtmp *.ctmp *.o mrbtest
	@$(MAKE) clean -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/bignum $(MAKE_FLAGS)
	@$(MAKE) clean -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/require $(MAKE_FLAGS)
	@$(MAKE) clean -C /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example $(MAKE_FLAGS)
