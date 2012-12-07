make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/src
rm -f ../lib/libmruby_core.a ./array.o ./ascii.o ./cdump.o ./class.o ./codegen.o ./compar.o ./crc.o ./dump.o ./encoding.o ./enum.o ./error.o ./etc.o ./gc.o ./gvar.o ./hash.o ./init.o ./init_ext.o ./kernel.o ./load.o ./math.o ./numeric.o ./object.o ./parse.o ./parse.tab.o ./pool.o ./print.o ./proc.o ./range.o ./re.o ./regcomp.o ./regenc.o ./regerror.o ./regexec.o ./regparse.o ./require.o ./sprintf.o ./st.o ./state.o ./string.o ./struct.o ./symbol.o ./time.o ./unicode.o ./us_ascii.o ./utf_8.o ./variable.o ./vm.o   ./y.tab.o ./y.tab.c
rm -f ./array.d ./ascii.d ./cdump.d ./class.d ./codegen.d ./compar.d ./crc.d ./dump.d ./encoding.d ./enum.d ./error.d ./etc.d ./gc.d ./gvar.d ./hash.d ./init.d ./init_ext.d ./kernel.d ./load.d ./math.d ./numeric.d ./object.d ./parse.d ./parse.tab.d ./pool.d ./print.d ./proc.d ./range.d ./re.d ./regcomp.d ./regenc.d ./regerror.d ./regexec.d ./regparse.d ./require.d ./sprintf.d ./st.d ./state.d ./string.d ./struct.d ./symbol.d ./time.d ./unicode.d ./us_ascii.d ./utf_8.d ./variable.d ./vm.d ./y.tab.d
rm -f ./y.tab.o ./minimain.o ./y.tab.d ./minimain.d
-- CLEAN mrbgems --
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -MMD -c /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.c -o /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.o
Build the generator which creates the driver and Gem Makefile
gcc -o /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.o
Cleanup Gems
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator makefile "GEMS.active" > g/Makefile
make clean -C g ACTIVE_GEMS="GEMS.active" MRUBY_ROOT='/Users/cmauceri/Documents/IOS/mruby'
rm -f *.c *.d *.rbtmp *.ctmp *.o mrbtest
rm -f mrb-c_and_ruby_extension_example-gem.a gem_mixlib.o gem_mixlib.c gem_mrblib_header.ctmp gem_mrblib_irep.ctmp gem_mixlib_init.ctmp gem_mrblib.rbtmp
Gem 'c_and_ruby_extension_example' is clean
rm -f gem_init.c *.o *.d /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator g/Makefile g/MakefileGemList gem_init.a
make clean -C ../../mrblib CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3'
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/mrblib
rm -f ../bin/mrbc mrblib.o mrblib.c mrblib.rbtmp mrblib.ctmp mrblib.d ../lib/libmruby.a
make clean -C ../mrbc CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3'
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/tools/mrbc
rm -f ../../bin/mrbc ../../src/../tools/mrbc/mrbc.o
rm -f ../../src/../tools/mrbc/mrbc.d
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/tools/mruby
rm -f /Users/cmauceri/Documents/IOS/mruby/bin/mruby /Users/cmauceri/Documents/IOS/mruby/src/../tools/mruby/mruby.o
rm -f /Users/cmauceri/Documents/IOS/mruby/src/../tools/mruby/mruby.d
make clean -C ../../mrblib CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3'
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/mrblib
rm -f ../bin/mrbc mrblib.o mrblib.c mrblib.rbtmp mrblib.ctmp mrblib.d ../lib/libmruby.a
make clean -C ../mrbc CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3'
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/tools/mrbc
rm -f ../../bin/mrbc ../../src/../tools/mrbc/mrbc.o
rm -f ../../src/../tools/mrbc/mrbc.d
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/tools/mirb
rm -f ../../bin/mirb ../../src/../tools/mirb/mirb.o
rm -f ../../src/../tools/mirb/mirb.d
make: removing targets, objects and depend files of /Users/cmauceri/Documents/IOS/mruby/test
rm -f mrbtest.o mrbtest.c mrbtest.rbtmp mrbtest.ctmp mrbtest.d driver.d driver.o mrbtest.o mrbtest
rm -f mrubytest.rb mrubytest.mrb mrubytest.rb.report mrubytest.mrb.report
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c array.c -o array.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c ascii.c -o ascii.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c cdump.c -o cdump.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c class.c -o class.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c codegen.c -o codegen.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c compar.c -o compar.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c crc.c -o crc.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c dump.c -o dump.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c encoding.c -o encoding.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c enum.c -o enum.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c error.c -o error.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c etc.c -o etc.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c gc.c -o gc.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c gvar.c -o gvar.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c hash.c -o hash.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c init.c -o init.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c init_ext.c -o init_ext.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c kernel.c -o kernel.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c load.c -o load.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c math.c -o math.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c numeric.c -o numeric.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c object.c -o object.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c parse.c -o parse.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c parse.tab.c -o parse.tab.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c pool.c -o pool.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c print.c -o print.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c proc.c -o proc.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c range.c -o range.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c re.c -o re.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c regcomp.c -o regcomp.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c regenc.c -o regenc.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c regerror.c -o regerror.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c regexec.c -o regexec.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c regparse.c -o regparse.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c require.c -o require.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c sprintf.c -o sprintf.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c st.c -o st.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c state.c -o state.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c string.c -o string.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c struct.c -o struct.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c symbol.c -o symbol.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c time.c -o time.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c unicode.c -o unicode.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c us_ascii.c -o us_ascii.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c utf_8.c -o utf_8.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c variable.c -o variable.o
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c vm.c -o vm.o
bison -o ./y.tab.c ./parse.y
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -c ./y.tab.c -o ./y.tab.o
ar r ../lib/libmruby_core.a ./array.o ./ascii.o ./cdump.o ./class.o ./codegen.o ./compar.o ./crc.o ./dump.o ./encoding.o ./enum.o ./error.o ./etc.o ./gc.o ./gvar.o ./hash.o ./init.o ./init_ext.o ./kernel.o ./load.o ./math.o ./numeric.o ./object.o ./parse.o ./parse.tab.o ./pool.o ./print.o ./proc.o ./range.o ./re.o ./regcomp.o ./regenc.o ./regerror.o ./regexec.o ./regparse.o ./require.o ./sprintf.o ./st.o ./state.o ./string.o ./struct.o ./symbol.o ./time.o ./unicode.o ./us_ascii.o ./utf_8.o ./variable.o ./vm.o   ./y.tab.o
cat ./*.rb > mrblib.rbtmp
make -C ../tools/mrbc CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3'
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I../../src -I../../src/../include -c ../../src/../tools/mrbc/mrbc.c -o ../../src/../tools/mrbc/mrbc.o
gcc -o ../../bin/mrbc ../../src/../tools/mrbc/mrbc.o ../../lib/libmruby_core.a -lm
../bin/mrbc -Bmrblib_irep -omrblib.ctmp mrblib.rbtmp; cat init_mrblib.c mrblib.ctmp > mrblib.c
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I../src -I../include -c mrblib.c -o mrblib.o
cp ../lib/libmruby_core.a ../lib/libmruby.a
ar r ../lib/libmruby.a mrblib.o
-- MAKE mrbgems --
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -MMD -c /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.c -o /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.o
Build the generator which creates the driver and Gem Makefile
gcc -o /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include /Users/cmauceri/Documents/IOS/mruby/mrbgems/generator.o
Generate Gem List Makefile
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator makefile_list "GEMS.active" > g/MakefileGemList
Generate Gem Makefile
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator makefile "GEMS.active" > g/Makefile
Build all gems
make -C g MRUBY_ROOT='/Users/cmauceri/Documents/IOS/mruby' ACTIVE_GEMS="GEMS.active"
Building Gem 'c_and_ruby_extension_example'
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator gem_mrblib "GEMS.active" > gem_mrblib_header.ctmp
cat mrblib/example.rb > gem_mrblib.rbtmp
/Users/cmauceri/Documents/IOS/mruby/bin/mrbc -Bgem_mrblib_irep_c_and_ruby_extension_example -ogem_mrblib_irep.ctmp gem_mrblib.rbtmp
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator gem_mixlib c_and_ruby_extension_example "GEMS.active" > gem_mixlib_init.ctmp
cat gem_mrblib_header.ctmp gem_mrblib_irep.ctmp gem_mixlib_init.ctmp > gem_mixlib.c
gcc -Isrc -I/Users/cmauceri/Documents/IOS/mruby/include -I/Users/cmauceri/Documents/IOS/mruby/src -I. -O3 -g -Wall -Werror-implicit-function-declaration   -c -o gem_mixlib.o gem_mixlib.c
ar rs mrb-c_and_ruby_extension_example-gem.a  src/example.o gem_mixlib.o
Generate Gem driver
/Users/cmauceri/Documents/IOS/mruby/mrbgems/generator gem_init "GEMS.active" > gem_init.c
Build the driver which initializes all gems
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I. -I./../include -MMD -c gem_init.c -o gem_init.o
ar rs gem_init.a gem_init.o
make -C tools/mruby --no-print-directory CC='gcc' LL='gcc' ALL_CFLAGS='-Wall -Werror-implicit-function-declaration -g -O3' ENABLE_GEMS='true' ACTIVE_GEMS='GEMS.active' MRUBY_ROOT='/Users/cmauceri/Documents/IOS/mruby'
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I/Users/cmauceri/Documents/IOS/mruby/src -I/Users/cmauceri/Documents/IOS/mruby/src/../include -c /Users/cmauceri/Documents/IOS/mruby/src/../tools/mruby/mruby.c -o /Users/cmauceri/Documents/IOS/mruby/src/../tools/mruby/mruby.o
gcc -o /Users/cmauceri/Documents/IOS/mruby/bin/mruby -g -O3 /Users/cmauceri/Documents/IOS/mruby/src/../tools/mruby/mruby.o /Users/cmauceri/Documents/IOS/mruby/lib/libmruby.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/gem_init.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example/mrb-c_and_ruby_extension_example-gem.a  -lm
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I../../src -I../../src/../include -c ../../src/../tools/mirb/mirb.c -o ../../src/../tools/mirb/mirb.o
gcc -o ../../bin/mirb -g -O3 ../../src/../tools/mirb/mirb.o ../../lib/libmruby.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/gem_init.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example/mrb-c_and_ruby_extension_example-gem.a  -lm
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I./../src -I./../include -c driver.c -o driver.o
cat ./assert.rb ./t/*.rb > mrbtest.rbtmp
../bin/mrbc -Bmrbtest_irep -omrbtest.ctmp mrbtest.rbtmp
cat /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example/test/*.rb  > mrbgemtest.rbtmp
/Users/cmauceri/Documents/IOS/mruby/bin/mrbc -Bmrbgemtest_irep -omrbgemtest.ctmp mrbgemtest.rbtmp
cat init_mrbtest.c mrbtest.ctmp ../mrbgems/g/mrbgemtest.ctmp > mrbtest.c
gcc -Wall -Werror-implicit-function-declaration -g -O3 -MMD -I./../src -I./../include -c mrbtest.c -o mrbtest.o
gcc -o mrbtest -g -O3 driver.o mrbtest.o ../lib/libmruby.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/gem_init.a /Users/cmauceri/Documents/IOS/mruby/mrbgems/g/c_and_ruby_extension_example/mrb-c_and_ruby_extension_example-gem.a -lm
cat ./assert.rb ./t/*.rb ./report.rb > mrubytest.rb
../bin/mrbc mrubytest.rb
# exec mrbtest
./mrbtest
mrbtest - Embeddable Ruby Test

This is a very early version, please test and report errors.
Thanks :)

.................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
Total: 497
   OK: 497
   KO: 0
Crash: 0
 Time: 0.005391 seconds

# exec mruby test with ruby script
mrubytest.rb success.

# exec mruby test with mrb file
mrubytest.mrb success.

