# Resolve only the pinned source target, never a host installation.
if(NOT TARGET de265)
  message(FATAL_ERROR "The pinned de265 source target must be built first")
endif()
set(LIBDE265_FOUND TRUE)
set(LIBDE265_VERSION 1.0.16)
set(LIBDE265_INCLUDE_DIRS "${pe_de265_SOURCE_DIR};${pe_de265_BINARY_DIR}")
set(LIBDE265_LIBRARIES de265)
