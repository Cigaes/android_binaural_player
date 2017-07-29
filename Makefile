# Binaural player
# © Nicolas George -- 2010
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
# Public License for more details.

PP = org/cigaes/binaural_player
APP = Binaural_player

RESOURCES = \
	res/drawable/icon.png \
	res/layout/tab_sequence.xml \
	res/layout/tab_edit.xml \
	res/layout/tab_play.xml
NATIVE_LIBS = \
	tmp/apk/lib/armeabi/libsbagen.so
CLASSES = \
	tmp/$(PP)/Binaural_player_GUI.class \
	tmp/$(PP)/Browser.class \
	tmp/$(PP)/Binaural_player.class \
	tmp/$(PP)/Binaural_decoder.class

SDK           = $(HOME)/local/android-sdk
ANDROID       = $(SDK)/platforms/android-7
NDK           = $(HOME)/local/android-ndk

HOST_ARCH = $(shell uname -s -m | tr 'A-Z ' a-z-)
TOOLCHAIN = arm-linux-androideabi-4.7

KEY_DEBUG     = -keystore $(HOME)/.android/debug.keystore \
	        -storepass android -keypass android
KEY_RELEASE   = -keystore $(HOME)/.android/cigaes.keystore
BOOTCLASSPATH = $(ANDROID)/android.jar
TOOLCHAINPATH = $(NDK)/toolchains/$(TOOLCHAIN)/prebuilt/$(HOST_ARCH)/bin
CC            = $(TOOLCHAINPATH)/arm-linux-androideabi-gcc
LIBGCC        = $(shell $(CC) -mthumb-interwork -print-libgcc-file-name)


JAVA_COMPILE = \
	LC_CTYPE=en_US.UTF-8 \
	javac -g -bootclasspath $(BOOTCLASSPATH) -classpath tmp -d tmp \
	-source 1.7 -target 1.7 $(JAVACFLAGS) $<
NATIVE_BUILD = \
	$(CC) \
	-I$(NDK)/platforms/android-3/arch-arm/usr/include \
	-fpic -mthumb-interwork -ffunction-sections -funwind-tables \
	-fstack-protector -fno-short-enums \
	-D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5E__ \
	-D__ARM_ARCH_5TE__ \
	-march=armv5te -mtune=xscale -msoft-float -mthumb \
	-fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 \
	-DANDROID
NATIVE_LINK = \
	$(CC) -nostdlib -Wl,-shared,-Bsymbolic \
	-o $@ $^ \
	$(LIBGCC) \
	-L$(NDK)/platforms/android-3/arch-arm/usr/lib \
	-llog \
	-lc \
	-lstdc++ \
	-lm \
	-Wl,--no-undefined \
	-Wl,-rpath-link=$(NDK)/platforms/android-3/arch-arm/usr/lib
AAPT_PACKAGE_BUILD_APK = \
	aapt package -f -M AndroidManifest.xml -S res -I $(BOOTCLASSPATH)

PATH := $(SDK)/build-tools/android-4.2.2:$(SDK)/tools:$(PATH)

all: $(APP)-debug.apk

release: $(APP).apk

clean:
	rm -f Binaural_player-debug.apk res/drawable/icon.png sbagen-test
	rm -rf tmp/*

upload-emul: $(APP)-debug.apk
	adb -e install -r $(APP)-debug.apk

upload-dev: $(APP)-debug.apk
	adb -d install -r $(APP)-debug.apk

tmp/$(PP)/R.java: AndroidManifest.xml $(RESOURCES)
	-mkdir -p tmp
	aapt package -m -J tmp -M AndroidManifest.xml -S res -I $(BOOTCLASSPATH)

tmp/apk/classes.dex: $(CLASSES)
	-mkdir -p tmp/apk
	rm -f tmp/Binaural_player-debug-unaligned.apk
	dx --dex --output=$@ tmp

tmp/$(APP)-debug-unaligned.apk: AndroidManifest.xml $(RESOURCES) \
  	tmp/apk/classes.dex $(NATIVE_LIBS)
	$(AAPT_PACKAGE_BUILD_APK) -F $@ tmp/apk
	jarsigner $(KEY_DEBUG) $@ androiddebugkey

tmp/$(APP)-unaligned.apk: AndroidManifest.xml $(RESOURCES) \
	tmp/apk/classes.dex $(NATIVE_LIBS)
	$(AAPT_PACKAGE_BUILD_APK) -F $@ tmp/apk
	jarsigner $(KEY_RELEASE) $@ cigaes

%.apk: tmp/%-unaligned.apk
	zipalign -f 4 $< $@

res/drawable/icon.png: icon.svg
	-mkdir -p res/drawable
	convert -background transparent -density 72 icon.svg -resize 10% -quality 99 $@

tmp/$(PP)/Binaural_player_GUI.class: Binaural_player_GUI.java
	$(JAVA_COMPILE)

tmp/$(PP)/Browser.class: Browser.java
	$(JAVA_COMPILE)

tmp/$(PP)/Binaural_player.class: Binaural_player.java
	$(JAVA_COMPILE)

tmp/$(PP)/Binaural_decoder.class: Binaural_decoder.java
	$(JAVA_COMPILE)

tmp/$(PP)/Binaural_player_GUI.class: tmp/$(PP)/R.java tmp/$(PP)/Browser.class
tmp/$(PP)/Binaural_player.class: tmp/$(PP)/Binaural_decoder.class

tmp/apk/lib/armeabi/libsbagen.so: tmp/sbagen.o
	-mkdir -p tmp/apk/lib/armeabi
	$(NATIVE_LINK)

tmp/sbagen.o: sbagen.c
	$(NATIVE_BUILD) -DBUILD_JNI=1 -O2 -c -o $@ $<

tmp/sbagen.o: tmp/sbagen.h

tmp/sbagen.h: tmp/$(PP)/Binaural_decoder.class
	javah -o $@ -classpath tmp org.cigaes.binaural_player.Binaural_decoder
	touch -c $@

sbagen-test: sbagen.c
	gcc -Wall -O2 -g -o $@ -DBUILD_STANDALONE_TEST=1 sbagen.c -lm
