#!/bin/bash
###########################################################################
#                                                                         #
# Copyright 2022 INTERSEC SA                                              #
#                                                                         #
# Licensed under the Apache License, Version 2.0 (the "License");         #
# you may not use this file except in compliance with the License.        #
# You may obtain a copy of the License at                                 #
#                                                                         #
#     http://www.apache.org/licenses/LICENSE-2.0                          #
#                                                                         #
# Unless required by applicable law or agreed to in writing, software     #
# distributed under the License is distributed on an "AS IS" BASIS,       #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.#
# See the License for the specific language governing permissions and     #
# limitations under the License.                                          #
#                                                                         #
###########################################################################

set -o pipefail

where="$1"
if which greadlink &> /dev/null; then
    readlink=greadlink
else
    readlink=readlink
fi
BUILD_DIR=$(dirname "$($readlink -f "$0")")
LIB_COMMON_DIR=$(dirname "$BUILD_DIR")
shift
BEHAVE_FLAGS="${BEHAVE_FLAGS:-}"

if test "$TERM" == "vt100" || # waf sets $TERM to vt100 when enabling colors
   (test "$TERM" != "dumb" -a -t 1 &&
    tput bold >/dev/null 2>&1 &&
    tput setaf 1 >/dev/null 2>&1 &&
    tput sgr0 >/dev/null 2>&1)
then
    say_color()
    {
        case "$1" in
            error) tput bold; tput setaf 1;;
            pass)  tput bold; tput setaf 2;;
            info)  tput setaf 3;;
        esac
        shift
        printf "%s" "$*"
        tput sgr0
        echo
    }

    post_process()
    {
        sed -e $'s/ pass / \x1B[1;32mpass\x1B[0m /' \
            -e $'s/ todo-pass / \x1B[1;33mtodo-pass\x1B[0m /' \
            -e $'s/ fail / \x1B[1;31mfail\x1B[0m /' \
            -e $'s/ todo-fail / \x1B[1;35mtodo-fail\x1B[0m /' \
            -e $'s/ skip / \x1B[1;30mskip\x1B[0m /' \
            -e $'s/#\(.*\)$/\x1B[1;30m#\\1\x1B[0m/' \
            -e $'s/^:\(.*\)/\x1B[1;31m: \x1B[1;33m\\1\x1B[0m/'
    }
else
    say_color()
    {
        shift 1
        echo "$@"
    }
    BEHAVE_FLAGS="${BEHAVE_FLAGS} --no-color"

    post_process()
    {
        cat
    }
fi

tmp=$(mktemp)
tmp2=$(mktemp)
corelist=$(mktemp)
trap "rm $tmp $tmp2 $corelist" 0

set_www_env() {
    if [[ "$Z_TAG_SKIP" =~ "web" ]] && [[ -z "$Z_TAG_OR" ]]; then
        return 0
    fi

    productdir=$($readlink -e "$1")
    htdocs="$productdir"/www/htdocs/

    [ -d $htdocs ] || return 0;

    z_www="${Z_WWW:-$(dirname "$LIB_COMMON_DIR")/www/www-spool}"
    index=$(basename "$productdir").php
    product=$(basename "$productdir")
    intersec_so=$(find $(dirname "$productdir") -name intersec.so -print -quit)

    Z_WWW_HOST="${_bkp_z_www_host:-$(hostname -f)}"
    Z_WWW_PREFIX="${_bkp_z_www_prefix:-zselenium-${product}}"
    Z_WWW_BROWSER="${_bkp_z_www_browser:-Remote}"

    # configure an apache website and add intersec.so to the php configuration
    make -C "$z_www" all htdocs=$htdocs index=$index intersec_so=$intersec_so \
                         host="${Z_WWW_PREFIX}.${Z_WWW_HOST}" product=$product \
                         productdir=$productdir
    if [ $? -ne 0 ]; then
        echo -e "****** Error ******\n"                                       \
            "To run web test suite you need to have some privileges:\n"       \
            " write access on: /etc/apache2/sites* | /etc/httpd/conf.d\n"     \
            " write access on: /etc/php5/conf.d | /etc/php.d/ \n"             \
            " sudoers without pwd on: /etc/init.d/apache2 | /etc/init.d/httpd"
        return 1
    fi
    export Z_WWW_HOST Z_WWW_PREFIX Z_WWW_BROWSER
}


"$(dirname "$0")"/list_checks.py "$where" | (
export Z_BEHAVE=1
export Z_HARNESS=1
export Z_TAG_SKIP="${Z_TAG_SKIP:-wip slow upgrade web perf}"
export Z_TAG_OR="${Z_TAG_OR:-}"
export Z_MODE="${Z_MODE:-fast}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-handle_segv=0}"
_bkp_z_www_prefix=${Z_WWW_PREFIX}
_bkp_z_www_host=${Z_WWW_HOST}
_bkp_z_www_browser=${Z_WWW_BROWSER}

for TAG_OR in ${Z_TAG_OR[@]}
do
    COMA_SEPARATED_TAGS=",$TAG_OR$COMA_SEPARATED_TAGS"
done

TAGS=($Z_TAG_SKIP)
for TAG in ${TAGS[@]}
do
     if [[ $TAG = "wip" ]]; then
         BEHAVE_TAGS="${BEHAVE_TAGS} --tags=-$TAG"
     else
         BEHAVE_TAGS="${BEHAVE_TAGS} --tags=-$TAG$COMA_SEPARATED_TAGS"
    fi
done
export BEHAVE_FLAGS="${BEHAVE_FLAGS} ${BEHAVE_TAGS} --format z --no-summary --no-capture-stderr"

coredump="$(dirname "$0")"/tests/core_dump.py

while read -r zd line; do
    t="${zd}${line}"
    say_color info "starting suite $t..."
    [ -n "$coredump" ] && $coredump list > $corelist

    start=$(date '+%s')
    case ./"$t" in
        */behave)
            productdir=$(dirname "./$t")
            res=1
            set_www_env $PWD/"$productdir"
            if [ $? -eq 0 ]; then
                "$BUILD_DIR/tests/zbehave.py" $BEHAVE_FLAGS "$productdir"/ci/features
                res=$?
            fi
            ;;
        *testem.json)
            cd $zd
            "$(dirname "$0")"/tests/ztestem.sh $line
            res=$?
            cd - &>/dev/null
            ;;
        */check_php)
            "$(dirname "$0")"/tests/check_php.py "$zd"
            res=$?
            ;;
        *)
            ./$t
            res=$?
            ;;
    esac
    [ -n "$coredump" ] && $coredump --format z -i @$corelist -r $PWD diff

    if [ $res -eq 0 ] ; then
        end=$(date '+%s')
        say_color pass "done ($((end - start)) seconds)"
    else
        end=$(date '+%s')
        say_color error "TEST SUITE $t FAILED ($((end - start)) seconds)"
    fi
done
) | tee $tmp | post_process

# Thanks to pipefail, it is not zero if at least one process failed
res=$?
if [ $res -ne 0 ]; then
    say_color error "check processes failed. check head or tail of log output"
fi

# whatever the previous status, set an error if a test failed
if ! "$BUILD_DIR/tests/zparser.py" "$tmp" > "$tmp2"; then
    res=1
fi
cat $tmp2 | post_process
exit $res
