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

.PHONY: create_appstore_screenshots
create_appstore_screenshots:
	scripts/create_appstore_screenshots.sh

# Rebuild whenever tests/functional_framework/container/Containerfile or
# entrypoint.sh change - NOT needed for app source changes, since every
# container run mounts (container/launch.sh) and copies (container/
# entrypoint.sh) the repo fresh.
.PHONY: build_functional_test_image
build_functional_test_image:
	tests/functional_framework/container/build_image.sh --conf tests/functional/app.conf

# PATTERN restricts which sequences run (forwarded as run_batch.sh's own
# --pattern) - e.g. `make test_functional_golden PATTERN='cron_*'`. Unset/
# empty (the default) runs every sequence, matching run_batch.sh's own
# default.
PATTERN ?=

# Plain pass/fail run of the whole functional suite, containerized (see
# tests/functional_framework/container/README.md for why this is required
# rather than just recommended) - no golden comparison.
.PHONY: test_functional
test_functional:
	tests/functional_framework/run_batch.sh --conf tests/functional/app.conf $(if $(PATTERN),--pattern '$(PATTERN)')

# What CI should run: the full suite, compared against the committed
# golden baseline.
.PHONY: test_functional_golden
test_functional_golden:
	tests/functional_framework/run_batch.sh --conf tests/functional/app.conf --golden-dir tests/functional/golden $(if $(PATTERN),--pattern '$(PATTERN)')

# Re-approves the current output of every sequence as the new golden
# baseline - run this after a deliberate, reviewed UI change, never as a
# way to make a failing test_functional_golden pass without looking at why.
.PHONY: update_functional_golden
update_functional_golden:
	tests/functional_framework/run_batch.sh --conf tests/functional/app.conf --golden-dir tests/functional/golden --update-golden $(if $(PATTERN),--pattern '$(PATTERN)')
