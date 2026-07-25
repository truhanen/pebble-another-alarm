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

.PHONY: build_and_install_emulator
build_and_install_emulator: build install_emulator

.PHONY: build_and_install_emulator_test
build_and_install_emulator_test: build_test install_emulator

.PHONY: build_and_install_cloudpebble
build_and_install_cloudpebble: build install_cloudpebble
