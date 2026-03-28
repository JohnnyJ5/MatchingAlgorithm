CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
BUILD    := build

# ── Algorithm object files ────────────────────────────────────────────────────
ALGO_SRCS := src/gale_shapley.cpp \
             src/hopcroft_karp.cpp \
             src/hungarian.cpp \
             src/blossom.cpp
ALGO_OBJS := $(patsubst src/%.cpp, $(BUILD)/%.o, $(ALGO_SRCS))

# ── Top-level targets ─────────────────────────────────────────────────────────
.PHONY: all tests run_tests run clean

all: $(BUILD)/main tests

# ── Main driver ───────────────────────────────────────────────────────────────
$(BUILD)/main: main.cpp $(ALGO_OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -I. $^ -o $@

# ── Unit tests ────────────────────────────────────────────────────────────────
TEST_BINS := $(BUILD)/test_gale_shapley \
             $(BUILD)/test_hopcroft_karp \
             $(BUILD)/test_hungarian \
             $(BUILD)/test_blossom

tests: $(TEST_BINS)

$(BUILD)/test_gale_shapley: src/tests/test_gale_shapley.cpp $(BUILD)/gale_shapley.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

$(BUILD)/test_hopcroft_karp: src/tests/test_hopcroft_karp.cpp $(BUILD)/hopcroft_karp.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

$(BUILD)/test_hungarian: src/tests/test_hungarian.cpp $(BUILD)/hungarian.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

$(BUILD)/test_blossom: src/tests/test_blossom.cpp $(BUILD)/blossom.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

# ── Compile algorithm objects ─────────────────────────────────────────────────
$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

# ── Run main driver ───────────────────────────────────────────────────────────
run: $(BUILD)/main
	./$(BUILD)/main

# ── Run all tests ─────────────────────────────────────────────────────────────
run_tests: tests
	@for t in $(TEST_BINS); do \
	    echo "--- $$t ---"; \
	    ./$$t; \
	    echo; \
	done

# ── Utility ───────────────────────────────────────────────────────────────────
$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
