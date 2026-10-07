CXX ?= c++

UNAME_S := $(shell uname -s)

CFLAT := -fno-unwind-tables			\
	 -fno-asynchronous-unwind-tables	\
	 -fvisibility=hidden			\
	 -fno-math-errno			\
	 -fno-trapping-math

ifeq ($(UNAME_S),Darwin)
LDFLAT := -nostdlib++
else
CFLAT += -fno-semantic-interposition
LDFLAT := -static-libgcc -nostdlib++
endif

ifeq ($(shell $(CXX) --version 2>/dev/null | grep -ci clang),0)
C99 := -Wno-pedantic -Wno-vla -Wno-missing-field-initializers
else
C99 := -Wno-c99-designator -Wno-c23-extensions -Wno-vla-cxx-extension	\
       -Wno-address-of-temporary -Wno-missing-field-initializers
endif

CXXFLAGS = -std=c++23 -O2 -fno-exceptions -fno-rtti $(CFLAT) $(C99)
LDFLAGS = $(LDFLAT)

BIN := bin
OBJ := $(BIN)/obj

.PHONY: benchmark benchmark-deps check clean fuzz fuzz-check tests
.SECONDARY: $(OBJ)/fuzz.o $(OBJ)/tests.o

check: $(BIN)/tests $(BIN)/fuzz $(BIN)/arena_capacity_regression
	./$(BIN)/tests
	./$(BIN)/arena_capacity_regression

fuzz-check: $(BIN)/fuzz
	sh tests/run_fuzz.sh ./$(BIN)/fuzz

benchmark:
	sh tests/run_benchmarks.sh

benchmark-deps:
	sh tests/setup_benchmark_deps.sh

clean:
	rm -rf ./bin

fuzz: $(BIN)/fuzz
tests: $(BIN)/tests

$(BIN) $(OBJ):
	mkdir -p $@

FLATLIB_SRCS := FlatJson.cpp
FLATLIB_OBJS := $(FLATLIB_SRCS:%.cpp=$(OBJ)/%.o)
FLATLIB_HDRS := FlatJson.hpp

$(OBJ)/%.o: %.cpp $(FLATLIB_HDRS) | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -I. -c -o $@ $<

$(OBJ)/%.o: tests/%.cpp $(FLATLIB_HDRS) | $(OBJ)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -I. -c -o $@ $<

$(BIN)/%: $(OBJ)/%.o $(FLATLIB_OBJS) | $(BIN)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) -o $@ $^
