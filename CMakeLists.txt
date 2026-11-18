# Read by ESP-IDF (and so esp-forge) as a component, and by any other CMake
# project that add_subdirectory()s this repo, such as a CubeMX-generated STM32
# one. stm-forge never reads it: it compiles every .c under src/, backend
# folders included, puts src/ on the include path of this library's sources
# and adds include/ -- which is why each port file, and every backend, is
# empty when its platform or option is off. ESP_PLATFORM tells the two apart;
# IDF sets it in its early requirements pass as well as in the build.

if(NOT ESP_PLATFORM)
    # A static library `servo`. Every .c under src/, as stm-forge does: a
    # backend file compiles to nothing unless its CONFIG_SERVO_<BACKEND> is 1,
    # so there is no source list here to keep in step with the IDF one below.
    file(GLOB_RECURSE servo_sources CONFIGURE_DEPENDS src/*.c)
    add_library(servo STATIC ${servo_sources})
    # include/ for the app; src/ only for this library's own sources, so a
    # backend folder reaches servo_priv.h by plain name and no private header
    # is ever on the app's include path.
    target_include_directories(servo PUBLIC include PRIVATE src)
    # roundf(), round() and fabsf() in the PCA9685 driver.
    target_link_libraries(servo PUBLIC m)

    # The options are the Kconfig bools, read from Kconfig so a new backend
    # needs no line here: a true CMake variable CONFIG_SERVO_<BACKEND> (or
    # CONFIG_SERVO_DEBUG) becomes CONFIG_SERVO_<BACKEND>=1, what stm-forge
    # passes. Only bools: an int option cannot be 1-or-absent. Each `config`
    # line in Kconfig is followed directly by its type line, which is what
    # the match relies on. PUBLIC, because servo_log.h reads
    # CONFIG_SERVO_DEBUG in the app's sources.
    file(READ Kconfig servo_kconfig)
    string(REGEX MATCHALL "config SERVO_[A-Z0-9_]+[ \t\r\n]+bool" servo_options
           "${servo_kconfig}")
    list(TRANSFORM servo_options REPLACE
         "config (SERVO_[A-Z0-9_]+)[ \t\r\n]+bool" "CONFIG_\\1")
    set(servo_backends_on "")
    foreach(option IN LISTS servo_options)
        if(${option})
            target_compile_definitions(servo PUBLIC ${option}=1)
            if(NOT option STREQUAL "CONFIG_SERVO_DEBUG")
                list(APPEND servo_backends_on ${option})
            endif()
        endif()
    endforeach()
    if(NOT servo_backends_on)
        message(WARNING "servo: no backend is selected; "
                        "set(CONFIG_SERVO_<BACKEND> ON) before add_subdirectory()")
    endif()

    # The HAL is the app's. MCU_HAL_TARGET names a target carrying its include
    # dirs and defines (USE_HAL_DRIVER, the device, the hal_conf.h directory)
    # -- stm32cubemx in a CubeMX-generated project; MCU_DEVICE_HEADER is the
    # family header the public headers include as CMSIS_device_header, which
    # CubeMX does not define. Both are shared by every mcu-* library, so an
    # app sets them once. Left unset, the app provides them its own way.
    #
    # Only the target's include dirs and defines are copied, the target is
    # never linked: CubeMX up to 6.13 also hangs main.c, the HAL and the
    # startup file on stm32cubemx as INTERFACE sources, which linking would
    # compile into libservo.a. A misspelt name fails at generate time.
    if(MCU_HAL_TARGET)
        target_include_directories(servo PRIVATE
            $<TARGET_PROPERTY:${MCU_HAL_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(servo PRIVATE
            $<TARGET_PROPERTY:${MCU_HAL_TARGET},INTERFACE_COMPILE_DEFINITIONS>)
    endif()
    if(MCU_DEVICE_HEADER)
        target_compile_definitions(servo PUBLIC
            "CMSIS_device_header=\"${MCU_DEVICE_HEADER}\"")
    endif()
    return()
endif()

# ESP-IDF: the backend files are listed one by one, and PRIV_INCLUDE_DIRS
# "src" lets a backend folder reach servo_priv.h by plain name.
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
