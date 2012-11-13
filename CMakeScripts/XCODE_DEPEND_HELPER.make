# DO NOT EDIT
# This makefile makes sure all linkable targets are
# up-to-date with anything they link to
default:
	echo "Do not invoke directly"

# For each target create a dummy rule so the target does not have to exist
/Users/cmauceri/Documents/IOS/mruby/mrblib/Debug/libmruby.a:
/Users/cmauceri/Documents/IOS/mruby/mrblib/MinSizeRel/libmruby.a:
/Users/cmauceri/Documents/IOS/mruby/mrblib/RelWithDebInfo/libmruby.a:
/Users/cmauceri/Documents/IOS/mruby/mrblib/Release/libmruby.a:


# Rules to remove targets that are older than anything to which they
# link.  This forces Xcode to relink the targets from scratch.  It
# does not seem to check these dependencies itself.
PostBuild.libmruby_static.Debug:
PostBuild.xpcat.Debug:
/Users/cmauceri/Documents/IOS/mruby/tools/xpcat/Debug/xpcat:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/xpcat/Debug/xpcat


PostBuild.mrbc.Debug:
/Users/cmauceri/Documents/IOS/mruby/tools/mrbc/Debug/mrbc:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mrbc/Debug/mrbc


PostBuild.mruby.Debug:
PostBuild.libmruby_static.Debug: /Users/cmauceri/Documents/IOS/mruby/tools/mruby/Debug/mruby
/Users/cmauceri/Documents/IOS/mruby/tools/mruby/Debug/mruby:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Debug/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mruby/Debug/mruby


PostBuild.mirb.Debug:
PostBuild.libmruby_static.Debug: /Users/cmauceri/Documents/IOS/mruby/tools/mirb/Debug/mirb
/Users/cmauceri/Documents/IOS/mruby/tools/mirb/Debug/mirb:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Debug/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mirb/Debug/mirb


PostBuild.mrbtest.Debug:
PostBuild.libmruby_static.Debug: /Users/cmauceri/Documents/IOS/mruby/test/Debug/mrbtest
/Users/cmauceri/Documents/IOS/mruby/test/Debug/mrbtest:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Debug/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/test/Debug/mrbtest


PostBuild.libmruby_static.Release:
PostBuild.xpcat.Release:
/Users/cmauceri/Documents/IOS/mruby/tools/xpcat/Release/xpcat:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/xpcat/Release/xpcat


PostBuild.mrbc.Release:
/Users/cmauceri/Documents/IOS/mruby/tools/mrbc/Release/mrbc:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mrbc/Release/mrbc


PostBuild.mruby.Release:
PostBuild.libmruby_static.Release: /Users/cmauceri/Documents/IOS/mruby/tools/mruby/Release/mruby
/Users/cmauceri/Documents/IOS/mruby/tools/mruby/Release/mruby:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Release/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mruby/Release/mruby


PostBuild.mirb.Release:
PostBuild.libmruby_static.Release: /Users/cmauceri/Documents/IOS/mruby/tools/mirb/Release/mirb
/Users/cmauceri/Documents/IOS/mruby/tools/mirb/Release/mirb:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Release/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mirb/Release/mirb


PostBuild.mrbtest.Release:
PostBuild.libmruby_static.Release: /Users/cmauceri/Documents/IOS/mruby/test/Release/mrbtest
/Users/cmauceri/Documents/IOS/mruby/test/Release/mrbtest:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/Release/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/test/Release/mrbtest


PostBuild.libmruby_static.MinSizeRel:
PostBuild.xpcat.MinSizeRel:
/Users/cmauceri/Documents/IOS/mruby/tools/xpcat/MinSizeRel/xpcat:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/xpcat/MinSizeRel/xpcat


PostBuild.mrbc.MinSizeRel:
/Users/cmauceri/Documents/IOS/mruby/tools/mrbc/MinSizeRel/mrbc:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mrbc/MinSizeRel/mrbc


PostBuild.mruby.MinSizeRel:
PostBuild.libmruby_static.MinSizeRel: /Users/cmauceri/Documents/IOS/mruby/tools/mruby/MinSizeRel/mruby
/Users/cmauceri/Documents/IOS/mruby/tools/mruby/MinSizeRel/mruby:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/MinSizeRel/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mruby/MinSizeRel/mruby


PostBuild.mirb.MinSizeRel:
PostBuild.libmruby_static.MinSizeRel: /Users/cmauceri/Documents/IOS/mruby/tools/mirb/MinSizeRel/mirb
/Users/cmauceri/Documents/IOS/mruby/tools/mirb/MinSizeRel/mirb:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/MinSizeRel/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mirb/MinSizeRel/mirb


PostBuild.mrbtest.MinSizeRel:
PostBuild.libmruby_static.MinSizeRel: /Users/cmauceri/Documents/IOS/mruby/test/MinSizeRel/mrbtest
/Users/cmauceri/Documents/IOS/mruby/test/MinSizeRel/mrbtest:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/MinSizeRel/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/test/MinSizeRel/mrbtest


PostBuild.libmruby_static.RelWithDebInfo:
PostBuild.xpcat.RelWithDebInfo:
/Users/cmauceri/Documents/IOS/mruby/tools/xpcat/RelWithDebInfo/xpcat:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/xpcat/RelWithDebInfo/xpcat


PostBuild.mrbc.RelWithDebInfo:
/Users/cmauceri/Documents/IOS/mruby/tools/mrbc/RelWithDebInfo/mrbc:
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mrbc/RelWithDebInfo/mrbc


PostBuild.mruby.RelWithDebInfo:
PostBuild.libmruby_static.RelWithDebInfo: /Users/cmauceri/Documents/IOS/mruby/tools/mruby/RelWithDebInfo/mruby
/Users/cmauceri/Documents/IOS/mruby/tools/mruby/RelWithDebInfo/mruby:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/RelWithDebInfo/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mruby/RelWithDebInfo/mruby


PostBuild.mirb.RelWithDebInfo:
PostBuild.libmruby_static.RelWithDebInfo: /Users/cmauceri/Documents/IOS/mruby/tools/mirb/RelWithDebInfo/mirb
/Users/cmauceri/Documents/IOS/mruby/tools/mirb/RelWithDebInfo/mirb:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/RelWithDebInfo/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/tools/mirb/RelWithDebInfo/mirb


PostBuild.mrbtest.RelWithDebInfo:
PostBuild.libmruby_static.RelWithDebInfo: /Users/cmauceri/Documents/IOS/mruby/test/RelWithDebInfo/mrbtest
/Users/cmauceri/Documents/IOS/mruby/test/RelWithDebInfo/mrbtest:\
	/Users/cmauceri/Documents/IOS/mruby/mrblib/RelWithDebInfo/libmruby.a
	/bin/rm -f /Users/cmauceri/Documents/IOS/mruby/test/RelWithDebInfo/mrbtest


