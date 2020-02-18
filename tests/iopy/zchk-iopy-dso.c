/***************************************************************************/
/*                                                                         */
/* Copyright 2019 INTERSEC SA                                              */
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

#include <Python.h>

#include <dlfcn.h>

#include <lib-common/iop.h>
#include <lib-common/z.h>

#include "zchk-iopy-dso.fc.c"

/* LCOV_EXCL_START */

#if PY_MAJOR_VERSION >= 3

# define IOPY_DSO_NAME  "iopy/python3/iopy.so"
# define T_PYSTRING_TO_CSTR(_obj)                                            \
    ({                                                                       \
        PyObject *_utf8 = PyUnicode_AsUTF8String(_obj);                      \
        char *_res = t_strdup(PyBytes_AsString(_utf8));                      \
                                                                             \
        Py_DECREF(_utf8);                                                    \
        _res;                                                                \
    })

#else
# define IOPY_DSO_NAME  "iopy/python2/iopy.so"
# define T_PYSTRING_TO_CSTR(_obj)  t_strdup(PyBytes_AsString(_obj))
#endif

static struct {
    void *iopy_dso;
    PyObject *plugin;
    PyObject *plugin_register;
    PyObject *script_globals;
#define _G  zchk_add_package_g
} zchk_add_package_g;

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
    lstr_t entry;
    PyObject *obj;
    PyObject *iopy_module;
    PyObject *script;
    lstr_t plugin_file;
    const char *iopy_dso_path;

    /* Get farch entry */
    entry = t_farch_get_data(zchk_iopy_dso, "zchk-iopy-dso.py");
    if (!entry.s) {
        e_fatal("unable to get entry zchk-iopy-dso.py");
    }

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

    /* Get plugin */
    plugin_file = t_lstr_fmt("%*pMtestsuite/test-iop-plugin.so",
                             LSTR_FMT_ARG(z_cmddir_g));
    _G.plugin = PyObject_CallMethod(iopy_module, (char *)"Plugin",
                                    (char *)"s#", plugin_file.s,
                                    plugin_file.len);
    if (!_G.plugin) {
        e_fatal("unable to create plugin from iopy module: %s",
                t_z_fetch_py_err());
    }

    /* Get plugin register */
    _G.plugin_register = PyObject_CallMethod(_G.plugin, (char *)"register",
                                             NULL);
    if (!_G.plugin_register) {
        e_fatal("unable to create plugin register from plugin: %s",
                t_z_fetch_py_err());
    }

    /* Get iopy_dso */
    iopy_dso_path = t_fmt("%*pM" IOPY_DSO_NAME, LSTR_FMT_ARG(z_cmddir_g));
    _G.iopy_dso = dlopen(iopy_dso_path,
                         RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    if (!_G.iopy_dso) {
        e_fatal("unable to dlopen iopy module at `%s`, it was not previously "
                "loaded by the python script", iopy_dso_path);
    }

    /* Set builtins to globals */
    _G.script_globals = PyDict_New();
    if (PyDict_SetItemString(_G.script_globals, "__builtins__",
                             PyEval_GetBuiltins()) < 0)
    {
        e_fatal("unable to get python __builtins__");
    }

    /* Run script */
    script = PyRun_String(entry.s, Py_file_input, _G.script_globals,
                          _G.script_globals);
    if (!script) {
        e_fatal("unable to start zchk-iopy-dso.py: %s", t_z_fetch_py_err());
    }

    Py_DECREF(script);
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
    Py_DECREF(_G.script_globals);
    Py_DECREF(_G.plugin_register);
    Py_DECREF(_G.plugin);
    Py_Finalize();
}

static int z_run_py_test(const char *name, sb_t *err)
{
    t_scope;
    PyObject *func = PyDict_GetItemString(_G.script_globals, name);
    PyObject *res;

    if (!func) {
        sb_setf(err, "unable to get test function with name `%s`", name);
        return -1;
    }

    res = PyObject_CallFunctionObjArgs(func, _G.plugin, _G.plugin_register,
                                       NULL);
    if (!res) {
        sb_setf(err, "%s", t_z_fetch_py_err());
        return -1;
    }
    Py_DECREF(res);

    return 0;
}

typedef void (*add_iop_package_f)(const iop_pkg_t *p, void *plugin);

Z_GROUP_EXPORT(iopy_dso) {
    Z_TEST(add_iop_package, "") {
        t_scope;
        int res = -1;
        SB_1k(err);
        add_iop_package_f add_iop_package_cb;
        const char *dso_path;
        iop_dso_t *dso;

        z_iopy_dso_initialize();

        add_iop_package_cb = dlsym(_G.iopy_dso, "Iopy_add_iop_package");
        Z_ASSERT_P(add_iop_package_cb, "unable to get symbol "
                   "Iopy_add_iop_package: %s", dlerror());

        dso_path = t_fmt("%*pMtestsuite/test-iop-plugin-dso.so",
                         LSTR_FMT_ARG(z_cmddir_g));
        dso = iop_dso_open(dso_path, LM_ID_BASE, &err);
        Z_ASSERT_P(dso, "%*pM", SB_FMT_ARG(&err));

        qm_for_each_pos(iop_pkg, pos, &dso->pkg_h) {
            add_iop_package_cb(dso->pkg_h.values[pos], _G.plugin);
        }

        res = z_run_py_test("test_add_iop_package", &err);

        iop_dso_close(&dso);
        Z_ASSERT_N(res, "%*pM", SB_FMT_ARG(&err));
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
