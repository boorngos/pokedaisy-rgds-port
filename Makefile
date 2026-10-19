# PokeDaisy Linux (Anbernic RG DS)
BUILD ?= build
CMAKE ?= cmake
JOBS ?= $(shell nproc)

.PHONY: all configure build preview shots run demo clean help

help:
	@echo "make          - configure + build"
	@echo "make demo     - run the companion demo (no ROM)"
	@echo "make shots    - dual-screen PNGs into web/shots"
	@echo "make run      - game list (puts ROMs in ~/.local/share/pokedaisy/roms)"

all: build

configure:
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release

build: configure
	$(CMAKE) --build $(BUILD) -j$(JOBS)

preview: build

shots: build
	mkdir -p web/shots
	$(BUILD)/pokedaisy-preview share/pokedaisy/demo/firered_vanilla BPRE web/shots firered
	$(BUILD)/pokedaisy-preview share/pokedaisy/demo/emerald_vanilla BPEE web/shots emerald
	$(BUILD)/pokedaisy-preview share/pokedaisy/demo/emerald_de_battle BPEE web/shots emerald-battle
	$(BUILD)/pokedaisy --demo firered --shot web/shots/rgds-demo.png

demo: build
	$(BUILD)/pokedaisy --layout window --demo firered

run: build
	$(BUILD)/pokedaisy --layout window

clean:
	rm -rf $(BUILD) web/shots
