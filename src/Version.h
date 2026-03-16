#pragma once

/// Major version number.
#define RIFT_VERSION_MAJOR 0
/// Minor version number.
#define RIFT_VERSION_MINOR 2
/// Patch version number.
#define RIFT_VERSION_PATCH 0
/// Release version number.
#define RIFT_VERSION_RELEASE 0

/**
 * Stringify version components.
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

/// Complete version string (e.g., "0.1.0.0").
#define RIFT_VERSION        \
    RIFT_VERSION_STRINGIFY( \
        RIFT_VERSION_MAJOR, RIFT_VERSION_MINOR, RIFT_VERSION_PATCH, RIFT_VERSION_RELEASE)
