/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

#define PY_SSIZE_T_CLEAN 1

/* 1. Defining PY_SSIZE_T_CLEAN triggers some 'redundant-decls' errors in
 *    Python.h with GCC.
 * 2. Python 3.12+ uses C99 declarations after statements.
 * So we need to ignore these warnings manually.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
# include <Python.h>
#pragma GCC diagnostic pop

#include <dlfcn.h>

#include <lib-common/iop.h>
#include <lib-common/z.h>

#include <lib-common/core/core.iop.h>
#include <lib-common/iop/ic.iop.h>
#include "testsuite/test.iop.h"
#include "testsuite/tst1.iop.h"
#include "testsuite/testvoid.iop.h"

#include "zchk-iopy-dso.fc.c"

/* LCOV_EXCL_START */

# define IOPY_DSO_NAME  "iopy.so"
# define T_PYSTRING_TO_CSTR(_obj)                                            \
    ({                                                                       \
        PyObject *_utf8 = PyUnicode_AsUTF8String(_obj);                      \
        char *_res = t_strdup(PyBytes_AsString(_utf8));                      \
                                                                             \
        Py_DECREF(_utf8);                                                    \
        _res;                                                                \
    })

static struct {
    iop_env_t *iop_env;
    void *iopy_dso;
#define _G  zchk_add_package_g
} zchk_add_package_g;


typedef PyObject *(*make_plugin_from_iop_env_f)(iop_env_t *iop_env);
typedef int (*add_iop_dso_f)(const iop_dso_t *dso, void *plugin);

static const char *t_z_fetch_traceback_err(PyObject *type, PyObject *value,
                                           PyObject *tb)
{
    t_SB_1k(sb);
    PyObject *module;
    PyObject *list_errs;
    Py_ssize_t list_size;

    module = PyImport_ImportModule("traceback");
    RETHROW_P(module);

    list_errs = PyObject_CallMethod(module, (char *)"format_exception",
                                    (char *)"OOO", type, value, tb);
    Py_DECREF(module);
    RETHROW_P(list_errs);

    list_size = PyList_Size(list_errs);
    for (Py_ssize_t i = 0; i < list_size; i++) {
        PyObject *err = PyList_GET_ITEM(list_errs, i);
        const char *err_desc = T_PYSTRING_TO_CSTR(err);

        sb_adds(&sb, err_desc);
    }

    Py_DECREF(list_errs);
    return sb.data;
}

static const char *t_z_fetch_py_err(void)
{
    const char *res;
    PyObject *type = NULL;
    PyObject *value = NULL;
    PyObject *tb = NULL;

    PyErr_Fetch(&type, &value, &tb);

    if (type == NULL) {
        /* No exceptions. */
        return "";
    }

    res = t_z_fetch_traceback_err(type, value, tb);
    if (!res) {
        if (!value) {
            res = "";
        } else {
            PyObject *str = PyObject_Str(value);

            res = T_PYSTRING_TO_CSTR(str);
            Py_DECREF(str);
        }
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(tb);
    return res;
}

static void z_iopy_dso_initialize_(void)
{
    t_scope;
    PyObject *obj;
    PyObject *iopy_module;
    const char *iopy_dso_path;

    /* Build the IOP environment */
    _G.iop_env = iop_env_new();

    IOP_REGISTER_PACKAGES(_G.iop_env, &test__pkg, &tst1__pkg, &ic__pkg,
                          &core__pkg, &testvoid__pkg);

    Py_Initialize();

    /* Add cmddir to python path */
    obj = PyUnicode_FromStringAndSize(z_cmddir_g.s, z_cmddir_g.len);
    if (PyList_Insert(PySys_GetObject((char *)"path"), 0, obj) < 0) {
        e_fatal("unable to insert cmddir to python path");
    }
    Py_DECREF(obj);

    /* Import iopy module */
    iopy_module = PyImport_ImportModule("iopy");
    if (!iopy_module) {
        e_fatal("unable to import iopy module");
    }

    /* Get iopy_dso */
    iopy_dso_path = t_fmt("%*pM" IOPY_DSO_NAME, LSTR_FMT_ARG(z_cmddir_g));
    _G.iopy_dso = dlopen(iopy_dso_path,
                         RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    if (!_G.iopy_dso) {
        e_fatal("unable to dlopen iopy module at `%s`, it was not previously "
                "loaded by the python script", iopy_dso_path);
    }

    Py_DECREF(iopy_module);
}

static void z_iopy_dso_initialize(void)
{
    if (!_G.iopy_dso) {
        z_iopy_dso_initialize_();
    }
}

static void z_iopy_dso_shutdown(void)
{
    if (!_G.iopy_dso) {
        return;
    }

    dlclose(_G.iopy_dso);
    Py_Finalize();
    iop_env_delete(&_G.iop_env);
}

static int z_load_plugin(PyObject **plugin_ptr)
{
    t_scope;
    make_plugin_from_iop_env_f make_plugin_cb;
    PyObject *plugin;

    /* Get the Iopy_make_plugin_iop_env function */
    make_plugin_cb = dlsym(_G.iopy_dso, "Iopy_make_plugin_iop_env");
    Z_ASSERT_P(make_plugin_cb,
               "unable to get symbol Iopy_make_plugin_iop_env: %s",
               dlerror());

    /* Build the plugin */
    plugin = (*make_plugin_cb)(_G.iop_env);
    Z_ASSERT_P(plugin, "unable to build the plugin: %s", t_z_fetch_py_err());

    /* Return the plugin */
    *plugin_ptr = plugin;

    Z_HELPER_END;
}

static int z_load_dso(PyObject *plugin, iop_dso_t **dso_ptr)
{
    SB_1k(err);
    add_iop_dso_f add_iop_dso_cb;
    const char *dso_path;
    iop_dso_t *dso;
    int res;

    /* Get the Iopy_add_iop_package from IOPy */
    add_iop_dso_cb = dlsym(_G.iopy_dso, "Iopy_add_iop_dso");
    Z_ASSERT_P(add_iop_dso_cb, "unable to get symbol "
               "Iopy_add_iop_dso: %s", dlerror());

    /* Open the test DSO */
    dso_path = t_fmt("%*pMtestsuite/test-iop-plugin-dso.so",
                     LSTR_FMT_ARG(z_cmddir_g));
    dso = iop_dso_open(_G.iop_env, dso_path, &err);
    Z_ASSERT_P(dso, "%*pM", SB_FMT_ARG(&err));

    /* Load the packages in the plugin */
    res = (*add_iop_dso_cb)(dso, plugin);
    Z_ASSERT_N(res, "unable to load the DSO: %s", t_z_fetch_py_err());

    /* Return the DSO */
    *dso_ptr = dso;

    Z_HELPER_END;
}

static int z_run_script(PyObject *plugin)
{
    t_scope;
    lstr_t entry;
    PyObject *script;
    PyObject *script_globals;
    PyObject *func;
    PyObject *res;

    /* Get farch entry */
    entry = t_farch_get_data(zchk_iopy_dso, "zchk-iopy-dso.py");
    Z_ASSERT_P(entry.s, "unable to get entry zchk-iopy-dso.py");

    /* Set builtins to globals */
    script_globals = PyDict_New();
    Z_ASSERT_N(PyDict_SetItemString(script_globals, "__builtins__",
                                    PyEval_GetBuiltins()),
               "unable to get python __builtins__");

    /* Run script */
    script = PyRun_String(entry.s, Py_file_input, script_globals,
                          script_globals);
    Z_ASSERT_P(script, "unable to start zchk-iopy-dso.py: %s",
               t_z_fetch_py_err());
    Py_DECREF(script);

    /* Get the function created by the script */
    func = PyDict_GetItemString(script_globals, "test_add_iop_package");
    Z_ASSERT_P(func, "unable to get test function with name "
               "`test_add_iop_package`");

    /* Call the function with the plugin */
    res = PyObject_CallFunctionObjArgs(func, plugin, NULL);
    Z_ASSERT_P(res, "%s", t_z_fetch_py_err());
    Py_DECREF(res);

    Py_DECREF(script_globals);
    Z_HELPER_END;
}

Z_GROUP_EXPORT(iopy_dso) {
    Z_TEST(iopy_c_func_load,
           "Load plugin and DSO through IOPy C external functions")
    {
        t_scope;
        PyObject *plugin = NULL;
        iop_dso_t *dso = NULL;

        /* Load IOPy module */
        z_iopy_dso_initialize();

        /* Load the plugin */
        Z_HELPER_RUN(z_load_plugin(&plugin));

        /* Load the DSO */
        Z_HELPER_RUN(z_load_dso(plugin, &dso));

        /* Run the script */
        Z_HELPER_RUN(z_run_script(plugin));

        /* Cleanup */
        Py_DECREF(plugin);
        iop_dso_close(&dso);
    } Z_TEST_END;

    z_iopy_dso_shutdown();
} Z_GROUP_END;

/* LCOV_EXCL_STOP */

int main(int argc, char **argv)
{
    z_setup(argc, argv);
    z_register_exports(PLATFORM_PATH LIBCOMMON_PATH "tests/iopy/");
    return z_run();
}
