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

asdf_log() {
    echo -e 2>&1 "... $@"
}

asdf_setup() {
    asdf_tools="$1/.tool-versions"

    asdf_log "installing ASDF plugins from '$asdf_tools'…"
    for asdf_plugin in $(awk '/^[^#]/ {print $1}' "$asdf_tools"); do
        # Note: `asdf plugin add` returns 1 in case of error, 2 if the plugin
        # is already up to date and 0 in case of successful install/update.
        asdf plugin-add "$asdf_plugin" || [ $? != 1 ]
    done

    asdf_log "installing ASDF versions…"
    asdf install
}

asdf_setup "$1"
