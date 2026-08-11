cmake_minimum_required(VERSION 3.20.0)

# Manual ANT SDK install: unzip sdk-ant to <NCS>/ant.
# Force-export as cache so sysbuild/child configure also sees it.
set(_ncs_roots "")
if(DEFINED ENV{WEST_TOPDIR})
	list(APPEND _ncs_roots "$ENV{WEST_TOPDIR}")
endif()
if(DEFINED ENV{ZEPHYR_BASE})
	get_filename_component(_zephyr_parent "$ENV{ZEPHYR_BASE}" DIRECTORY)
	list(APPEND _ncs_roots "${_zephyr_parent}")
endif()

set(_ant_module_path "")
list(REMOVE_DUPLICATES _ncs_roots)
foreach(_root IN LISTS _ncs_roots)
	if(EXISTS "${_root}/ant/zephyr/module.yml")
		set(_ant_module_path "${_root}/ant")
		break()
	endif()
endforeach()

if(_ant_module_path)
	set(ZEPHYR_EXTRA_MODULES "${_ant_module_path};${ZEPHYR_EXTRA_MODULES}" CACHE STRING "Extra Zephyr modules" FORCE)
	message(STATUS "Using ANT module: ${_ant_module_path}")
else()
	message(WARNING "ANT module not found under WEST_TOPDIR/ZEPHYR_BASE parent")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xds_t800_broker)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/central_t800.c
  src/leds.c
  src/gatt_callbacks.c
  src/gatt_battery.c
  src/gatt_system_info.c
  src/ant_profiles.c
  src/ant_bike_power.c
  src/shell_ant_slave.c
  src/shell_ant_master.c
  src/shell_flush.c
  src/shell_bt.c
  src/shell_poweroff.c
  src/bootloader.c
  src/battery_gauge.c
  src/watchdog.c
  src/poweroff.c
  src/crank_sim.c
  src/cdc_acm_serial.c
  src/main.c
)


# NORDIC SDK APP END
