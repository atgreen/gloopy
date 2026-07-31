# Convenience wrapper around the CMake + Ninja build. The real build system is CMake
# (see CMakeLists.txt); this just saves you remembering the incantations. `make help`
# lists everything. See AGENTS.md for the full story.

BUILD ?= build
BIN    := $(BUILD)/Gloopy_artefacts/Release/gloopy
CMAKE_CONFIGURE := cmake -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release

.DEFAULT_GOAL := build

# Configure once (auto-run the first time you build).
$(BUILD)/CMakeCache.txt:
	$(CMAKE_CONFIGURE)

.PHONY: build
build: $(BUILD)/CMakeCache.txt ## Build the Gloopy app (default)
	cmake --build $(BUILD) --target Gloopy

.PHONY: configure
configure: ## (Re)run CMake configure (Release, Ninja)
	$(CMAKE_CONFIGURE)

.PHONY: run
run: build ## Build then launch the app  (e.g. make run ARGS=song.gloopy)
	$(BIN) $(ARGS)

.PHONY: test
test: ## Build and run the unit tests + the headless smoke test
	cmake -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLOOPY_TESTS=ON
	cmake --build $(BUILD) --target GloopyTests
	ctest --test-dir $(BUILD) --output-on-failure
	./tests/smoke.sh

.PHONY: init
init: ## Fetch/stage the vendored Surge sources (once, on a fresh clone)
	bash scripts/init-surge.sh

.PHONY: clean
clean: ## Remove the build directory
	rm -rf $(BUILD)

.PHONY: help
help: ## List these targets
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
	  | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-10s\033[0m %s\n",$$1,$$2}'
