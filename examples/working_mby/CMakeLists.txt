cmake_minimum_required(VERSION 3.15)

include(proj.conf)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(.)

target_sources(app PRIVATE msc_ram_template.c)
target_sources(app PRIVATE hid_composite.c)
sdk_set_main_file(main.c)



project(usbd_msc_ram)

#project(fatfs)
