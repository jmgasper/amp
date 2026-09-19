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
LIBS = -lbe -lmedia -ltranslation -ltracker -llocalestub $(CORE_LIBS)
.PHONY: all core player package clean icon check
all: $(BUILD)/TasAmp
core: $(CORE_OBJ)
$(BUILD)/TasAmp: $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) resources/TasAmp.rdef resources/branding/tasamp-icon.hvif
	$(CXX) -o $@.new $(CORE_OBJ) $(PLAYER_OBJ) $(UI_OBJ) $(LIBS)
	rc -o $(BUILD)/TasAmp.rsrc resources/TasAmp.rdef
	xres -o $@.new $(BUILD)/TasAmp.rsrc
	mimeset -f $@.new
	mv $@.new $@
$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/tasamp_core_tests: $(CORE_OBJ) $(BUILD)/tests/CoreTests.o
	$(CXX) -o $@ $^ $(CORE_LIBS)
check: $(BUILD)/tasamp_core_tests
	$(BUILD)/tasamp_core_tests
icon:
	python3 tools/make-icon.py resources/branding/tasamp-icon.hvif resources/branding/tasamp-icon-preview.png
package: all
	bash tools/package-haiku.sh
clean:
	rm -rf $(BUILD)
-include $(CORE_OBJ:.o=.d) $(PLAYER_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(BUILD)/tests/CoreTests.d
player: $(PLAYER_OBJ)
