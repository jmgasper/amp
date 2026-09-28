# Native Haiku build; needs the development tools bundled with Haiku plus
# curl_devel, sqlite_devel and taglib2_devel (pkgman install ...).
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
CPPFLAGS += -Isrc -Isrc/player -Ivendor
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter -Wno-sign-compare
CORE = $(wildcard src/core/*.cpp)
PLAYER = $(wildcard src/player/*.cpp)
UI = $(wildcard src/ui/*.cpp) src/main.cpp
CORE_OBJ = $(CORE:%.cpp=$(BUILD)/%.o)
PLAYER_OBJ = $(PLAYER:%.cpp=$(BUILD)/%.o)
UI_OBJ = $(UI:%.cpp=$(BUILD)/%.o)
CORE_LIBS = -lcurl -lsqlite3 -ltag -lnetwork -lpthread
LIBS = -lbe -lmedia -ldevice -ltranslation -ltracker -llocalestub $(CORE_LIBS)
.PHONY: all core player package clean icon check mdtool
all: $(BUILD)/Amp
core: $(CORE_OBJ)
$(BUILD)/Amp: $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) resources/Amp.rdef resources/branding/amp-icon.hvif
	$(CXX) -o $@.new $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) $(LIBS)
	rc -o $(BUILD)/Amp.rsrc resources/Amp.rdef
	xres -o $@.new $(BUILD)/Amp.rsrc
	mimeset -f $@.new
	mv $@.new $@
$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/amp_core_tests: $(CORE_OBJ) $(BUILD)/tests/CoreTests.o
	$(CXX) -o $@ $^ $(CORE_LIBS)
check: $(BUILD)/amp_core_tests
	$(BUILD)/amp_core_tests
# command-line NetMD tool for testing against a real recorder
mdtool: $(BUILD)/mdtool
$(BUILD)/mdtool: $(CORE_OBJ) $(BUILD)/src/player/NetMDUsb.o $(BUILD)/src/player/MiniDiscPcm.o $(BUILD)/tools/mdtool.o
	$(CXX) -o $@ $^ -lbe -lmedia -ldevice $(CORE_LIBS)
icon:
	python3 tools/make-icon.py resources/branding/amp-icon.hvif resources/branding/amp-icon-preview.png
package: all
	bash tools/package-haiku.sh
clean:
	rm -rf $(BUILD)
-include $(CORE_OBJ:.o=.d) $(PLAYER_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(BUILD)/tests/CoreTests.d $(BUILD)/tools/mdtool.d
player: $(PLAYER_OBJ)
