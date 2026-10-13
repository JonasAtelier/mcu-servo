CC ?= cc
# gnu11: the servo_* log macros use `, ##__VA_ARGS__`.
CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
# Signed overflow usually wraps anyway, so a test of wrapping arithmetic
# passes on the broken code too. UBSan makes it abort instead.
CFLAGS += -fsanitize=undefined -fno-sanitize-recover=all

# src/ for the common private header, and every backend folder, because the
# tests include a backend's private headers by plain name.
CPPFLAGS += -I../include -I../src $(addprefix -I,$(wildcard ../src/*/))

# The core touches no platform code, so test_core runs once. The backends
# and the bus layer run twice: through src/servo_port_esp.c against
# idf_stubs/ (esp/) and through src/servo_port_stm.c against hal_stubs/
# (stm/). Their cases live once, in *_cases.h. test_pca9685_math.c touches
# nothing but the pure headers; test_names.c is every public header at once.

BACKENDS := -DCONFIG_SERVO_PWM=1 -DCONFIG_SERVO_PCA9685=1
# What ESP-IDF passes: ESP_PLATFORM, and every backend on.
ESP_CPPFLAGS := -Iidf_stubs -DESP_PLATFORM $(BACKENDS)
# What stm-forge passes: the family header the code includes, the HAL modules
# an app's hal_conf would enable, and every backend option on.
STM_CPPFLAGS := -Ihal_stubs \
		-DCMSIS_device_header='"stm32_hal_stub.h"' \
		-DHAL_I2C_MODULE_ENABLED -DHAL_SPI_MODULE_ENABLED \
		-DHAL_UART_MODULE_ENABLED -DHAL_TIM_MODULE_ENABLED \
		$(BACKENDS) -DCONFIG_SERVO_DEBUG=1

# The core and the stubs every binary of a suite links.
CORE     := ../src/servo.c
ESP_CORE := $(CORE) ../src/servo_port_esp.c idf_stubs/idf_stubs.c
STM_CORE := $(CORE) ../src/servo_port_stm.c hal_stubs/hal_stubs.c

PWM     := ../src/pwm/servo_pwm.c
PCA9685 := ../src/pca9685/pca9685.c

PURE_TESTS := test_core test_pca9685_math test_names
ESP_TESTS  := $(addprefix esp/,test_pwm test_pca9685 test_port_esp)
STM_TESTS  := $(addprefix stm/,test_pwm test_pca9685 test_port_stm)
TESTS      := $(PURE_TESTS) $(ESP_TESTS) $(STM_TESTS)

.PHONY: all clean test names

all: $(TESTS)

test_core: test_core.c $(CORE)
test_pca9685_math: test_pca9685_math.c
test_names: test_names.c

esp/test_pwm: esp/test_pwm.c $(PWM) $(ESP_CORE)
esp/test_pca9685: esp/test_pca9685.c $(PCA9685) $(ESP_CORE)
esp/test_port_esp: esp/test_port_esp.c $(ESP_CORE)

stm/test_pwm: stm/test_pwm.c $(PWM) $(STM_CORE)
stm/test_pca9685: stm/test_pca9685.c $(PCA9685) $(STM_CORE)
stm/test_port_stm: stm/test_port_stm.c $(STM_CORE)

$(PURE_TESTS) $(ESP_TESTS): CPPFLAGS += $(ESP_CPPFLAGS)
$(STM_TESTS): CPPFLAGS += $(STM_CPPFLAGS)

# Every printf() in a binary that links the HAL stubs also lands in
# stub_log_last, so a test can check what was logged.
$(STM_TESTS): LDFLAGS += -Wl,--wrap=printf

$(TESTS):
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@ -lm

# Every global symbol the library defines, on either platform, is servo_*
# or pca9685_*: apps link this next to the other category libraries, whose
# copies of the bus layer carry their own prefixes. Objects only, no stubs.
LIB_SRCS := $(CORE) ../src/servo_port_esp.c ../src/servo_port_stm.c $(PWM) \
	    $(PCA9685)
names:
	@set -e; tmp=$$(mktemp -d); trap 'rm -rf $$tmp' EXIT; \
	for s in $(LIB_SRCS); do \
		o=$$(basename $$s .c).o; \
		$(CC) $(CPPFLAGS) $(ESP_CPPFLAGS) -std=gnu11 -c $$s -o $$tmp/esp-$$o; \
		$(CC) $(CPPFLAGS) $(STM_CPPFLAGS) -std=gnu11 -c $$s -o $$tmp/stm-$$o; \
	done; \
	bad=$$(nm -g --defined-only $$tmp/*.o | awk 'NF == 3 { print $$3 }' | \
	       grep -Ev '^(servo_|pca9685_)' || true); \
	if [ -n "$$bad" ]; then echo "names: unprefixed globals:"; \
		echo "$$bad"; exit 1; fi; \
	echo "names: every global is servo_* or pca9685_*"

test: $(TESTS) names
	@set -e; for t in $(TESTS); do echo "./$$t"; ./$$t; done

clean:
	rm -f $(TESTS)

# Headers are prerequisites so that editing one rebuilds. Without this, `make
# test` reruns a stale binary and reports green for code it never compiled.
# Recipes use $(filter %.c,$^) so the headers never reach the compiler.
HDRS  := $(wildcard ../include/*.h ../src/*.h ../src/*/*.h)
STUBS := $(wildcard idf_stubs/*.h idf_stubs/*/*.h hal_stubs/*.h)
$(TESTS): $(HDRS) $(STUBS) $(wildcard *_cases.h)
