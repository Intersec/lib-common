#!/bin/sh -e

cc="$1"

clang_version="$("$cc" --version | grep 'clang version' | cut -d ' ' -f 3)"
version=$("$cc" -dumpfullversion -dumpversion)

prereq() {
    want="$1"
    has="$2"

    test "$want" = "$has" && return 0
    while test -n "$want"; do
        test -z "$has" && return 1
        w="${want%%.*}"
        h="${has%%.*}"
        case "$want" in *.*) want="${want#*.}";; *) want=""; esac
        case "$has" in *.*)  has="${has#*.}";;   *) has=""; esac

        test "$w" -lt "$h" && return 0
        test "$w" -gt "$h" && return 1
    done

    return 0
}

gcc_prereq()
{
    case "$cc" in
        cc*|gcc*|c++*|g++*) ;;
        *) return 1;
    esac
    prereq "$1" "$version"
}

is_clang()
{
    case "$cc" in
        clang*|*c*-analyzer) return 0;;
        *) return 1;;
    esac
}

clang_prereq()
{
    is_clang || return 1
    prereq "$1" "$clang_version"
}

is_cpp()
{
    case "$cc" in
        *++*) return 0;;
        *) return 1;;
    esac
}

get_internal_clang_args()
{
    while test $# != 0; do
        case "$1" in
            '"'-internal-*)
                echo $1
                echo $2
                shift 2
                ;;
            '"'-fgnuc-version=*)
                # keep this flag for clang 10
                echo $1
                shift 1
                ;;
            *)
                shift
                ;;
        esac
    done
}

build_flags()
{
    # use C99 to be able to for (int i =...
    if clang_prereq 3.3 || gcc_prereq 4.7; then
        ( is_cpp && echo -std=gnu++98 ) || echo -std=gnu11
    else
        ( is_cpp && echo -std=gnu++98 ) || echo -std=gnu99
    fi

    if is_clang; then
        if test "$2" != "rewrite"; then
            echo -fdiagnostics-show-category=name
        fi
        echo "-fblocks"
    else
        echo -funswitch-loops
        # ignore for (i = 0; i < limit; i += N) as dangerous for N != 1.
        echo -funsafe-loop-optimizations
        echo -fshow-column
        if gcc_prereq 4.3; then
            echo -fpredictive-commoning
            echo -ftree-vectorize
            echo -fgcse-after-reload
        fi
    fi

    if test "$2" != "rewrite"; then
        # know where the warnings come from
        echo -fdiagnostics-show-option
        # let the type char be unsigned by default
        echo -funsigned-char
        # do not use strict aliasing, pointers of different types may alias.
        echo -fno-strict-aliasing
    fi
    # let overflow be defined
    echo -fwrapv

    # turn on all common warnings
    echo -Wall
    echo -Wextra

    # treat warnings as errors but for a small few
    echo -Werror
    if gcc_prereq 4.3 || is_clang; then
        echo -Wno-error=deprecated-declarations
    else
        echo -Wno-deprecated-declarations
    fi
    if gcc_prereq 4.6; then
        cat <<EOF | $cc -c -x c -o /dev/null - >/dev/null 2>/dev/null || echo -D__gcc_has_no_ifunc
static int foo(void) { };
void (*bar(void))(void) __attribute__((ifunc("foo")));
EOF

    fi
    if is_clang; then
        echo -Wno-gnu-designator
        if clang_prereq 3.1; then
            echo -Wno-return-type-c-linkage
            echo -Wbool-conversion
            echo -Wempty-body
            echo -Wloop-analysis
            echo -Wsizeof-array-argument
            echo -Wstring-conversion
            echo -Wparentheses
        fi
        if clang_prereq 3.3; then
            echo -Wduplicate-enum
        fi
        if clang_prereq 3.4; then
            echo -Wheader-guard
            echo -Wlogical-not-parentheses

            if is_cpp; then
                echo -Wno-extern-c-compat
            fi
        fi
        if clang_prereq 3.7; then
            echo -Wno-nullability-completeness
            echo -Wno-shift-negative-value
        fi
        if clang_prereq 3.9; then
            echo -Wcomma
            echo -Wfloat-overflow-conversion
            echo -Wfloat-zero-conversion
        fi
    fi

    echo -Wchar-subscripts
    # warn about undefined preprocessor identifiers
    echo -Wundef
    # warn about local variable shadowing another local variable
    echo -Wshadow -D'"index(s,c)=index__(s,c)"'
    # make string constants const
    echo -Wwrite-strings
    # warn about implicit conversions with side effects
    # fgets, calloc and friends take an int, not size_t...
    #echo -Wconversion
    # warn about comparisons between signed and unsigned values, but not on
    echo -Wsign-compare
    # warn about unused declared stuff
    echo -Wunused
    # do not warn about unused function parameters
    echo -Wno-unused-parameter
    # warn about variable use before initialization
    echo -Wuninitialized
    # warn about variables which are initialized with themselves
    echo -Winit-self
    if gcc_prereq 4.5; then
        echo -Wenum-compare
        echo -Wlogical-op
    fi
    if gcc_prereq 4.6; then
        #echo -flto -fuse-linker-plugin
        echo -Wsuggest-attribute=noreturn
    fi
    # warn about pointer arithmetic on void* and function pointers
    echo -Wpointer-arith
    # warn about multiple declarations
    echo -Wredundant-decls
    if gcc_prereq 4.2; then
        # XXX: this is disabled for 4.1 because we sometimes need to disable it
        #      and GCC diagostic pragmas is a 4.2 feature only. As gcc-4.1 is no
        #      longer used for development this isn't a problem.
        # warn if the format string is not a string literal
        echo -Wformat-nonliteral
    fi
    # do not warn about strftime format with y2k issues
    echo -Wno-format-y2k
    # warn about functions without format attribute that should have one
    echo -Wmissing-format-attribute
    # barf if we change constness
    #echo -Wcast-qual
    if gcc_prereq 6.0; then
        echo -Wno-shift-negative-value
    fi

    if gcc_prereq 11.0; then
        # do not warn about stringop-overflow which has a lot of
        # false-positive, see
        # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=88443
        echo -Wno-stringop-overflow
        # do not warn about stringop-overread which has a lot of
        # false-positive, see
        # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=97048
        echo -Wno-stringop-overread
    fi

    if is_cpp; then
        if test "$2" != "rewrite"; then
            echo -fno-rtti
            echo -fno-exceptions
        fi
        if gcc_prereq 4.2; then
            echo -Wnon-virtual-dtor
            echo -Woverloaded-virtual
        else
            echo -Wno-shadow
        fi
        if gcc_prereq 6.0; then
            echo -Wno-c++11-compat
        elif gcc_prereq 4.9; then
            echo -Wno-extern-c-compat
        fi
        if gcc_prereq 8.0; then
            # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89722
            # The usage of typeof in OP_BIT is causing some const problems,
            # only with g++, and this warning is not very interesting anyway.
            echo -Wno-ignored-qualifiers
            # See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89729
            # We are doing bad things like p_clear or memcpy on c++ objects
            # like script_data_t, but not a big deal.
            echo -Wno-class-memaccess
        fi
    else
        # warn about functions declared without complete a prototype
        echo -Wstrict-prototypes
        echo -Wmissing-prototypes
        echo -Wmissing-declarations
        # warn about extern declarations inside functions
        echo -Wnested-externs
        # warn when a declaration is found after a statement in a block
        echo -Wdeclaration-after-statement
        # do not warn about zero-length formats.
        echo -Wno-format-zero-length
    fi

    echo -D_GNU_SOURCE # $(getconf LFS_CFLAGS)

    if test "$2" = rewrite; then
        get_internal_clang_args $("$cc" -x c${cc#clang} -'###' /dev/null 2>&1 | grep 'cc1')
    fi
}

if test "$2" = "rewrite"; then
    # Fails if clang does not support block rewriting
    "$cc" -cc1 -rewrite-blocks < /dev/null > /dev/null
fi

build_flags "$@" | tr '\n' ' '
