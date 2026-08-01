.PHONY: clean
clean:
	pebble clean

.PHONY: build
build:
	pebble build || pebble build

.PHONY: build_test
build_test:
	APP_TEST_HOOKS=1 pebble build || APP_TEST_HOOKS=1 pebble build

.PHONY: kill_emulator
kill_emulator:
	pebble kill

.PHONY: wipe_emulator
wipe_emulator:
	pebble wipe

.PHONY: install_emulator
install_emulator:
	pebble install --emulator emery

.PHONY: install_cloudpebble
install_cloudpebble:
	pebble install --cloudpebble

# Simulates a long press (past this app's 500ms long-click threshold, e.g.
# the cron/repeat editors' long-press-SELECT/BACK shortcuts) on the running
# emulator. Usage: make long_press_emulator BUTTON=select
BUTTON ?= select
.PHONY: long_press_emulator
long_press_emulator:
	pebble emu-button --duration 700 click $(BUTTON)

.PHONY: build_and_install_emulator
build_and_install_emulator: build install_emulator

.PHONY: build_and_install_emulator_test
build_and_install_emulator_test: build_test install_emulator

.PHONY: build_and_install_cloudpebble
build_and_install_cloudpebble: build install_cloudpebble

.PHONY: create_screenshots
create_screenshots:
	scripts/create_screenshots.sh
