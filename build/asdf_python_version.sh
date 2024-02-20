#!/bin/bash -eu
##########################################################################
#                                                                        #
#  Copyright (C) INTERSEC SA                                             #
#                                                                        #
#  Should you receive a copy of this source code, you must check you     #
#  have a proper, written authorization of INTERSEC to hold it. If you   #
#  don't have such an authorization, you must DELETE all source code     #
#  files in your possession, and inform INTERSEC of the fact you obtain  #
#  these files. Should you not comply to these terms, you can be         #
#  prosecuted in the extent permitted by applicable law.                 #
#                                                                        #
##########################################################################

# File to compute the Python versions to be used in ASDF .tool-versions.

tool_versions_semver_ge() {
    printf '%s\n%s\n' "$2" "$1" | sort --check=quiet --version-sort
}

tool_versions_debug_log() {
    echo -e "... ASDF .tool-versions: $*" >&2
}

tool_versions_get_python3_system_version() {
    # Use a subshell here to avoid modifying the current shell environment
    (
        # If we already are in a subshell, remove it from the PATH
        if [[ -n "${VIRTUAL_ENV:-}" ]]; then
            PATH="${PATH##"$VIRTUAL_ENV"/bin:}"
        fi

        # Remove the potential ASDF python plugin and install directories
        # '.asdf/*/python/*' from the PATH.
        # Since waf can be started with a previous python version controlled
        # by ASDF, ASDF can put some directory in the PATH when running waf
        # that points to the old python version. We need to clean them to
        # really use the python system version.
        local asdf_data_dir="${ASDF_DATA_DIR:-$HOME/.asdf}"
        PATH="$(echo "$PATH" | \
            sed -E "s|$asdf_data_dir/?[^/]*/python/[^:]*:||g")"

        # Export modified PATH
        export PATH

        # Force using system version if python is already handled by ASDF
        export ASDF_PYTHON_VERSION='system'

        # Get the python3 system version.
        # If it is not available, we will return '0.0.0'.
        local cmd
        cmd="$(echo -e \
            'import sys;' \
            'print(f"{sys.version_info[0]}.{sys.version_info[1]}."' \
            '      f"{sys.version_info[2]}")' \
        )"
        python3 -c "$cmd" 2>/dev/null || echo '0.0.0'
    )
}

main() {
    local default_version="$1"
    local min_version="${2:-0.0.1}"
    local max_version="${3:-99.99.99}"
    local system_version

    # Get the python system version
    system_version="$(tool_versions_get_python3_system_version)"

    # Check if the system version is between the min and max versions
    if tool_versions_semver_ge "$system_version" "$min_version" && \
        tool_versions_semver_ge "$max_version" "$system_version"
    then
        # If is, use the python system version
        tool_versions_debug_log \
            "Using python system version $system_version"
        echo "system"
    else
        # Else, use the default python version
        tool_versions_debug_log \
            "Using default python version $default_version instead of" \
            "python system version $system_version"
        echo "$default_version system"
    fi
}

main "$@"
