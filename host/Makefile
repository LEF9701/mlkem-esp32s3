# Build the host (Ubuntu) version of the ML-KEM benchmark.
#   make run              -> ML-KEM-768
#   make run LEVEL=512    -> ML-KEM-512   (also 1024)

LEVEL ?= 768
MLKEM := ../mlkem-native/mlkem
DEFS  := -DMLK_CONFIG_PARAMETER_SET=$(LEVEL) -DMLK_CONFIG_NAMESPACE_PREFIX=mlkem
INC   := -I$(MLKEM) -I../common

CFLAGS   := -O2 -std=c99   -Wall -Wextra $(DEFS) $(INC)
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra $(DEFS) $(INC)

BUILD := build-$(LEVEL)

.PHONY: all run clean
all: $(BUILD)/mlkem_lab

$(BUILD)/mlkem_native.o: $(MLKEM)/mlkem_native.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/main.o: main.cpp ../common/mlkem.hpp ../common/experiments.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/mlkem_lab: $(BUILD)/main.o $(BUILD)/mlkem_native.o
	$(CXX) $^ -o $@

run: all
	./$(BUILD)/mlkem_lab

clean:
	rm -rf build-*
