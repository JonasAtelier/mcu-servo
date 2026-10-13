# ESP-IDF only. stm-forge never reads this file: it compiles every .c under
# src/, backend folders included, puts src/ on the include path of this
# library's sources and adds include/ -- which is why each port file, and
# every backend, is empty when its platform or option is off. Here the
# backend files are listed one by one, and PRIV_INCLUDE_DIRS "src" lets a
# backend folder reach servo_priv.h by plain name.
#
# The core is always built, even with CONFIG_SERVO off; each backend only
# when its Kconfig option is on. Kconfig values are available here during
# the build phase, and only SRCS depends on them -- never REQUIRES, which IDF
# resolves before sdkconfig. That is also why SERVO off still builds the
# core: an empty SRCS would need a second, differently-shaped register call,
# and an unreferenced core costs nothing once --gc-sections has run.
#
# Every global symbol is servo_* or pca9685_*, the copied bus layer
# included, so an app links this next to the other category libraries.
#
# REQUIRES driver, not esp_driver_*: the split components do not exist on
# IDF 5.2, and driver still pulls them in on later versions; LEDC and I2C
# are there.
set(srcs "src/servo.c" "src/servo_port_esp.c")

if(CONFIG_SERVO AND NOT (CONFIG_SERVO_PWM OR CONFIG_SERVO_PCA9685))
    message(WARNING "servo: CONFIG_SERVO is on but no backend is selected; "
                    "set CONFIG_SERVO_<BACKEND>=y (esp-forge: SERVO_<BACKEND> = y)")
endif()

if(CONFIG_SERVO_PWM)
    list(APPEND srcs "src/pwm/servo_pwm.c")
endif()
if(CONFIG_SERVO_PCA9685)
    list(APPEND srcs "src/pca9685/pca9685.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES driver)
