## What is pxcc

Pxcc is a tool to export C types and symbols specified in a `.pxc` file to a
Cython definition file `.pxd`.

## What is a `.pxc` file

A `.pxc` file is a C file describing which C types and symbols should be
exported to a Cython definition file. Here is an example of a `toto.pxc`
file:
```
#include "toto.h"

PXCC_EXPORT_FILE("toto.h");

PXCC_EXPORT_TYPE(my_struct_t);
PXCC_EXPORT_SYMBOL(my_func);
PXCC_EXPORT_SYMBOL(MY_CONST);
```

A `.pxc` is internally interpreted as a C header file, so you must first
`#include` all the headers containing the types and symbols you want to
export.
Next, you need to specify the **unique** header file Cython will use to
look for all the exported types and symbols with `PXCC_EXPORT_FILE()`.
Usually you want to `#include` and `PXCC_EXPORT_FILE()` the same header file.

Then you can finally specify which C types you want to export with
`PXCC_EXPORT_TYPE()`, and which C symbols you want to export with
`PXCC_EXPORT_SYMBOL()`.

All the types used by the specified types and symbols will be exported as well
recursively.

**Warning:** pxcc cannot export macros because they are not part of the AST
produced by clang and the types cannot be resolved. So if you want to export
a macro to Cython, you need to manually do it.
https://cython.readthedocs.io/en/latest/src/userguide/external_C_code.html

## Result of pxcc

The result file of the pxcc tool is a `.pxd` cython definition file.
In our previous example, we have `toto.h`:
```
typedef struct my_struct_t {
    int a;
    double b;
    const char *c;
} my_struct_t;

typedef unsigned long long u64_t;

typedef union my_func_arg_t {
    long unsigned int a;
    void *b;
    u64_t c;
} my_func_arg_t;

void *my_func(my_func_arg_t *arg);

typedef enum my_enum_t {
    MY_ENUM_A,
    MY_ENUM_B = 0x12,
    MY_ENUM_C = -70,
} my_enum_t;

extern const my_enum_t MY_CONST;
```

The result of the command `pxcc toto.pxc -o toto.pxd` will be a `toto.pxd`:
```
from libcpp cimport bool as _Bool

cdef extern from "toto.h" nogil:

    cdef struct my_struct_t:
        int a
        double b
        const char *c

    ctypedef unsigned long long u64_t

    cdef union my_func_arg_t:
        unsigned long a
        void *b
        u64_t c

    void *my_func(my_func_arg_t *)

    cdef enum my_enum_t:
        MY_ENUM_A = 0,
        MY_ENUM_B = 18,
        MY_ENUM_C = -70,

    const my_enum_t MY_CONST
  ```
As you can see, all the types used by `my_func` and `MY_CONST` are also
exported.

You can then use `toto.pxd` in your Cython module file `.pyx`:
```
from toto cimport *

def foo():
    print(sizeof(my_struct_t))
```

## How it works internally

We use `libclang` to construct and go through the AST.
`PXCC_EXPORT_FILE()`, `PXCC_EXPORT_TYPE()`, `PXCC_EXPORT_SYMBOL()` are macros
that will be catch by pxcc.
We first parse and detect the types and symbols to export, and then print them
in the correct Cython format in the order of dependencies.

## TODO

Pxcc does not accept macros. This could be done with `libclang` but requires a
lot of digging into `libclang` documentation and will have a lot of
restrictions since we can only manipulate real C types with Cython.

## Known bugs

Pxcc fails to export the following struct:
```
struct broken_t {
    const struct broken_t *(*plop)(int);
};
```
