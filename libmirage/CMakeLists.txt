cmake_minimum_required (VERSION 2.8.5)

# Project name
project (libmirage C)

# Release versioning:
set (MIRAGE_VERSION_MAJOR 3)
set (MIRAGE_VERSION_MINOR 2)
set (MIRAGE_VERSION_MICRO 4)
set (MIRAGE_VERSION_LONG ${MIRAGE_VERSION_MAJOR}.${MIRAGE_VERSION_MINOR}.${MIRAGE_VERSION_MICRO})
set (MIRAGE_VERSION_SHORT ${MIRAGE_VERSION_MAJOR}.${MIRAGE_VERSION_MINOR})

# Semantic versioning 2.0.0 (semver.org):
# MAJOR is increased when backwards-incompatible changes are made to
# the API (e.g., removal of interfaces). In this case, MINOR and PATCH
# should be reset to zero.
# MINOR is increased when features are added to the API (e.g., addition
# of interfaces). In this case, PATCH should be reset to zero.
# PATCH is increased for all other changes such as bug-fixes.
set (MIRAGE_SOVERSION_MAJOR 11)
set (MIRAGE_SOVERSION_MINOR 0)
set (MIRAGE_SOVERSION_PATCH 0)

set (MIRAGE_SOVERSION ${MIRAGE_SOVERSION_MAJOR}.${MIRAGE_SOVERSION_MINOR}.${MIRAGE_SOVERSION_PATCH})

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (GTKDOC_ENABLED "Use gtk-doc to build documentation" on)
option (INTROSPECTION_ENABLED "Generate gobject introspection files" on)
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Plugin directory
set (MIRAGE_PLUGIN_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/libmirage-${MIRAGE_VERSION_SHORT}" CACHE PATH "Path to libMirage plugin directory." FORCE)

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)
find_package (IntlTool 0.21 REQUIRED)
find_package (Gettext 0.15 REQUIRED)

pkg_check_modules (GLIB REQUIRED glib-2.0>=2.38 gobject-2.0>=2.38 gmodule-2.0>=2.38 gio-2.0>=2.38)

if (INTROSPECTION_ENABLED)
    pkg_check_modules (INTROSPECTION gobject-introspection-1.0>=1.30.0)
    if (INTROSPECTION_FOUND)
        include (GObjectIntrospection)
        set (INTROSPECTION_STATUS "ON")
    else ()
        set (INTROSPECTION_STATUS "OFF (not found)")
    endif ()
else ()
    set (INTROSPECTION_STATUS "OFF (disabled)")
endif ()

if (GTKDOC_ENABLED)
    pkg_check_modules (GTKDOC gtk-doc>=1.4)
    if (GTKDOC_FOUND)
        include (GtkDoc)
        set (GTKDOC_STATUS "ON")
    else ()
        set (GTKDOC_STATUS "OFF (not found)")
    endif ()
else ()
    set (GTKDOC_STATUS "OFF (disabled)")
endif ()

if (POST_INSTALL_HOOKS)
    find_program(UPDATE_MIME_DATABASE_EXECUTABLE NAMES update-mime-database)
    mark_as_advanced(UPDATE_MIME_DATABASE_EXECUTABLE)
endif ()


# Auto-generated files
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/mirage/version.h.in
    ${PROJECT_BINARY_DIR}/mirage/version.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/reference/version.xml.in
    ${PROJECT_BINARY_DIR}/reference/version.xml
)
configure_file (
    ${PROJECT_SOURCE_DIR}/libmirage.pc.in
    ${PROJECT_BINARY_DIR}/libmirage.pc
    @ONLY
)

# Global definitions
add_definitions (-std=c99) # We're compiling using C99
add_definitions (-DHAVE_CONFIG_H)

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions (-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    add_definitions (-Wno-sign-compare)
    add_definitions (-Wno-cast-function-type)
endif ()

# Definitions needed for localization
add_definitions (-DGETTEXT_PACKAGE="${PROJECT_NAME}")
add_definitions (-DLOCALEDIR="${CMAKE_INSTALL_FULL_DATADIR}/locale")

# Global include directories
include_directories (${PROJECT_SOURCE_DIR})
include_directories (${PROJECT_BINARY_DIR})

include_directories (${GLIB_INCLUDE_DIRS})

# Link directories
link_directories (${GLIB_LIBRARY_DIRS})

# *** libMirage core library ***
set (libmirage_HEADERS
    mirage/mirage.h
    mirage/cdtext-coder.h
#    mirage/compat-input-stream.h
    mirage/context.h
    mirage/contextual.h
    mirage/debug.h
    mirage/disc.h
    mirage/error.h
    mirage/file-stream.h
    mirage/filter-stream.h
    mirage/fragment.h
    mirage/index.h
    mirage/language.h
    mirage/object.h
    mirage/parser.h
    mirage/plugin.h
    mirage/sector.h
    mirage/session.h
    mirage/stream.h
    mirage/track.h
    mirage/types.h
    mirage/utils.h
    mirage/writer.h
    ${PROJECT_BINARY_DIR}/mirage/version.h
)

set (libmirage_SOURCES
    mirage/mirage.c
    mirage/cdtext-coder.c
    mirage/compat-input-stream.c
    mirage/context.c
    mirage/contextual.c
    mirage/disc.c
    mirage/error.c
    mirage/file-stream.c
    mirage/filter-stream.c
    mirage/fragment.c
    mirage/index.c
    mirage/language.c
    mirage/object.c
    mirage/parser.c
    mirage/plugin.c
    mirage/sector.c
    mirage/session.c
    mirage/stream.c
    mirage/track.c
    mirage/utils.c
    mirage/version.c
    mirage/writer.c
)

add_library (mirage SHARED ${libmirage_SOURCES})
target_link_libraries (mirage ${GLIB_LIBRARIES})

set_target_properties (mirage PROPERTIES
    LIBRARY_OUTPUT_NAME mirage
    VERSION ${MIRAGE_SOVERSION}
    SOVERSION ${MIRAGE_SOVERSION_MAJOR}
)

install (TARGETS mirage DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (FILES ${libmirage_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libmirage-${MIRAGE_VERSION_MAJOR}.${MIRAGE_VERSION_MINOR}/mirage)

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})

# *** Introspection ***
if (INTROSPECTION_ENABLED AND INTROSPECTION_FOUND)
    gobject_introspection (
        FILENAME Mirage-${MIRAGE_VERSION_MAJOR}.${MIRAGE_VERSION_MINOR}.gir
        NSVERSION ${MIRAGE_VERSION_SHORT}
        INCLUDES GObject-2.0 Gio-2.0 GLib-2.0 GModule-2.0
        LIBRARY mirage
        QUIET
        SCANNER_ARGS --add-include-path=${PROJECT_SOURCE_DIR} --warn-all
        COMPILER_ARGS --includedir=${PROJECT_SOURCE_DIR}
        SYMBOL_PREFIXES mirage
        IDENTIFIER_PREFIXES Mirage
        SOURCES ${libmirage_SOURCES} ${libmirage_HEADERS}
    )
endif ()

# *** Documentation ***
if (GTKDOC_ENABLED AND GTKDOC_FOUND)
    get_target_property (GTKDOC_LIB mirage LIBRARY_OUTPUT_NAME)
    gtk_doc (
        MODULE ${PROJECT_NAME}
        MAIN_SGML_FILE ${PROJECT_NAME}-docs.sgml
        SOURCE_DIR ${PROJECT_SOURCE_DIR}/mirage ${PROJECT_BINARY_DIR}/mirage
        SOURCES ${libmirage_HEADERS} ${libmirage_SOURCES}
        DOCS_DIR ${PROJECT_SOURCE_DIR}/reference
        IGNORE_HFILES "compat-input-stream.h"
        CFLAGS ${GLIB_CFLAGS} -I${PROJECT_SOURCE_DIR}/mirage
        LDFLAGS ${GLIB_LDFLAGS} -L${PROJECT_BINARY_DIR}
            -Wl,-rpath,${PROJECT_BINARY_DIR} -l${GTKDOC_LIB}
    )
endif ()

# *** Pkg-config ***
install (
    FILES ${PROJECT_BINARY_DIR}/libmirage.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# *** Filters ***
file(GLOB filters RELATIVE ${PROJECT_SOURCE_DIR}/filters/ ${PROJECT_SOURCE_DIR}/filters/*)
foreach (filter ${filters})
    string(REGEX REPLACE "^filter-" "" filter_name ${filter})
    list(FIND FILTERS_DISABLED ${filter_name} filter_disabled)
    if (filter_disabled EQUAL -1)
        add_subdirectory(filters/filter-${filter_name})
    endif ()
endforeach ()
if (FILTERS_ENABLED)
    list(SORT FILTERS_ENABLED)
endif ()
if (FILTERS_DISABLED)
    list(SORT FILTERS_DISABLED)
endif()

# *** Images ***
file(GLOB images RELATIVE ${PROJECT_SOURCE_DIR}/images/ ${PROJECT_SOURCE_DIR}/images/*)
foreach (image ${images})
    string(REGEX REPLACE "^image-" "" image_name ${image})
    list(FIND IMAGE_FORMATS_DISABLED ${image_name} image_disabled)
    if (image_disabled EQUAL -1)
        add_subdirectory(images/image-${image_name})
    endif ()
endforeach ()
if (IMAGE_FORMATS_ENABLED)
    list(SORT IMAGE_FORMATS_ENABLED)
endif ()
if (IMAGE_FORMATS_DISABLED)
    list(SORT IMAGE_FORMATS_DISABLED)
endif ()

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** libMirage v.${MIRAGE_VERSION_LONG} configuration summary ***")
message(STATUS "Compiler and build info:")
message(STATUS " C compiler: " ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
message(STATUS " C flags (Release): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE})
message(STATUS " C flags (Debug): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG})
message(STATUS " build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS " plugin dir: " ${MIRAGE_PLUGIN_DIR})
message(STATUS "File filters:")
message(STATUS " enabled: ${FILTERS_ENABLED}")
message(STATUS " disabled: ${FILTERS_DISABLED}")
message(STATUS "Image formats:")
message(STATUS " enabled: ${IMAGE_FORMATS_ENABLED}")
message(STATUS " disabled: ${IMAGE_FORMATS_DISABLED}")
message(STATUS "Options:")
message(STATUS " build gtk-doc documentation: " ${GTKDOC_STATUS})
message(STATUS " build gobject-introspection bindings: " ${INTROSPECTION_STATUS})
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
