cmake_minimum_required(VERSION 3.28...3.30)

add_library(corridor-key-2.0-filter MODULE)
add_library(OBS::corridor-key-2.0-filter ALIAS corridor-key-2.0-filter)

target_sources(
  corridor-key-2.0-filter
  PRIVATE
    corridor-key-filter.c
)

target_link_libraries(corridor-key-2.0-filter PRIVATE OBS::libobs $<$<PLATFORM_ID:Windows>:OBS::w32-pthreads>)

set_target_properties_obs(corridor-key-2.0-filter PROPERTIES FOLDER plugins PREFIX "")
