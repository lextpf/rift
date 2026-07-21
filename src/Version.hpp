#pragma once

/**
 * @brief The single source of truth for the engine's 4-part version.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * MAJOR.MINOR.PATCH.RELEASE. CMake parses these four `#define` lines to derive the
 * project version, so edit the numbers here and nowhere else - and keep each on one
 * line in this exact form.
 */

/// @ingroup Core
/// Major version number.
#define RIFT_VERSION_MAJOR 0
/// Minor version number.
#define RIFT_VERSION_MINOR 2
/// Patch version number.
#define RIFT_VERSION_PATCH 0
/// Release version number.
#define RIFT_VERSION_RELEASE 0

/**
 * @brief Stringify version components.
 *
 * Two levels are required: the inner macro applies `#`, and the outer one exists
 * solely to force its arguments to expand first, so the version macros stringify to
 * their values rather than to their own names.
 *
 * @param major Major version.
 * @param minor Minor version.
 * @param patch Patch version.
 * @param release Release version.
 *
 * @see RIFT_VERSION
 */
#define RIFT_VERSION_STRINGIFY_(major, minor, patch, release) \
    #major "." #minor "." #patch "." #release
#define RIFT_VERSION_STRINGIFY(major, minor, patch, release) \
    RIFT_VERSION_STRINGIFY_(major, minor, patch, release)

/// Complete dotted version string (`"MAJOR.MINOR.PATCH.RELEASE"`), built from the four macros
/// above. Used for the window title, the title-screen footer, and the `version` command.
#define RIFT_VERSION        \
    RIFT_VERSION_STRINGIFY( \
        RIFT_VERSION_MAJOR, RIFT_VERSION_MINOR, RIFT_VERSION_PATCH, RIFT_VERSION_RELEASE)
