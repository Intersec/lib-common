/***** THIS FILE IS AUTOGENERATED DO NOT MODIFY DIRECTLY ! *****/
#ifndef IOP_HEADER_GUARD_tstjson_TYPEDEFS_H
#define IOP_HEADER_GUARD_tstjson_TYPEDEFS_H

#include <lib-common/core.h>
#include <lib-common/iop/internals.h>

typedef enum tstjson__my_enum_a__t {
    A_A,
    A_B,
    A_C = 100,
    A_D,
} tstjson__my_enum_a__t;
typedef IOP_ARRAY_OF(enum tstjson__my_enum_a__t) tstjson__my_enum_a__array_t;
#ifdef OPT_OF
typedef OPT_OF(enum tstjson__my_enum_a__t) tstjson__my_enum_a__opt_t;
#else
typedef IOP_OPT_OF(enum tstjson__my_enum_a__t) tstjson__my_enum_a__opt_t;
#endif
#define A_count 4
#define A_min   0
#define A_max   101

typedef enum tstjson__my_enum_b__t {
    MY_ENUM_B_A,
    MY_ENUM_B_B,
    MY_ENUM_B_C,
} tstjson__my_enum_b__t;
typedef IOP_ARRAY_OF(enum tstjson__my_enum_b__t) tstjson__my_enum_b__array_t;
#ifdef OPT_OF
typedef OPT_OF(enum tstjson__my_enum_b__t) tstjson__my_enum_b__opt_t;
#else
typedef IOP_OPT_OF(enum tstjson__my_enum_b__t) tstjson__my_enum_b__opt_t;
#endif
#define MY_ENUM_B_count 3
#define MY_ENUM_B_min   0
#define MY_ENUM_B_max   2

typedef enum tstjson__my_enum_c__t {
    MY_ENUM_C_A,
    MY_ENUM_C_B,
} tstjson__my_enum_c__t;
typedef IOP_ARRAY_OF(enum tstjson__my_enum_c__t) tstjson__my_enum_c__array_t;
#ifdef OPT_OF
typedef OPT_OF(enum tstjson__my_enum_c__t) tstjson__my_enum_c__opt_t;
#else
typedef IOP_OPT_OF(enum tstjson__my_enum_c__t) tstjson__my_enum_c__opt_t;
#endif
#define MY_ENUM_C_count 2
#define MY_ENUM_C_min   0
#define MY_ENUM_C_max   1

typedef struct tstjson__my_union_a__t tstjson__my_union_a__t;
typedef IOP_ARRAY_OF(tstjson__my_union_a__t) tstjson__my_union_a__array_t;

typedef struct tstjson__my_struct_a__t tstjson__my_struct_a__t;
typedef IOP_ARRAY_OF(tstjson__my_struct_a__t) tstjson__my_struct_a__array_t;

typedef struct tstjson__my_struct_b__t tstjson__my_struct_b__t;
typedef IOP_ARRAY_OF(tstjson__my_struct_b__t) tstjson__my_struct_b__array_t;

typedef struct tstjson__my_struct_c__t tstjson__my_struct_c__t;
typedef IOP_ARRAY_OF(tstjson__my_struct_c__t) tstjson__my_struct_c__array_t;

typedef struct tstjson__constraint_u__t tstjson__constraint_u__t;
typedef IOP_ARRAY_OF(tstjson__constraint_u__t) tstjson__constraint_u__array_t;

typedef struct tstjson__constraint_s__t tstjson__constraint_s__t;
typedef IOP_ARRAY_OF(tstjson__constraint_s__t) tstjson__constraint_s__array_t;

typedef struct tstjson__my_class_base__t tstjson__my_class_base__t;
typedef IOP_ARRAY_OF(tstjson__my_class_base__t *nonnull ) tstjson__my_class_base__array_t;

typedef struct tstjson__my_class_a__t tstjson__my_class_a__t;
typedef IOP_ARRAY_OF(tstjson__my_class_a__t *nonnull ) tstjson__my_class_a__array_t;

typedef struct tstjson__my_class_b__t tstjson__my_class_b__t;
typedef IOP_ARRAY_OF(tstjson__my_class_b__t *nonnull ) tstjson__my_class_b__array_t;

typedef struct tstjson__my_class1__t tstjson__my_class1__t;
typedef IOP_ARRAY_OF(tstjson__my_class1__t *nonnull ) tstjson__my_class1__array_t;

typedef struct tstjson__my_class2__t tstjson__my_class2__t;
typedef IOP_ARRAY_OF(tstjson__my_class2__t *nonnull ) tstjson__my_class2__array_t;

typedef struct tstjson__class_container__t tstjson__class_container__t;
typedef IOP_ARRAY_OF(tstjson__class_container__t) tstjson__class_container__array_t;

typedef struct tstjson__my_exception_a__t tstjson__my_exception_a__t;
typedef IOP_ARRAY_OF(tstjson__my_exception_a__t) tstjson__my_exception_a__array_t;

/*----- structure/union for typedef MyUnionInt (from tstjson.MyUnionA)-----*/
typedef tstjson__my_union_a__t tstjson__my_union_int__t;
typedef tstjson__my_union_a__array_t tstjson__my_union_int__array_t;
#define tstjson__my_union_int__s tstjson__my_union_a__s
#define tstjson__my_union_int__sp tstjson__my_union_a__sp

#endif