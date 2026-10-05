###########################################################################
#                                                                         #
# Copyright 2026 INTERSEC SA                                              #
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
# This file is loaded during early waf bootstrap, before the project's
# Python toolchain is set up, so it must remain Python 3.6 compatible.
# That rules out PEP 585 generics (`list[...]`) and PEP 604 unions (`X | Y`);
# stick to `typing.List` here.
# ruff: noqa: UP006, UP035, UP045

"""
Contains the `iop-pystubs` command.

When invoked, it scans every loaded task generator for those whose
`shlib(...)` declaration sets `pystub_path=`, then narrows the
build to that list of DSO targets. Building those DSOs triggers the
`DsoPystubTask` plumbing in `backend.py`, which (re)generates the
matching `*__iop.pyi` files in the `pystub_path=` directory of each DSO.

The discovery is dynamic, so new pystub-producing targets added in
any `wscript_build` are picked up automatically — no list to update
here.
"""

import os.path as osp
from typing import Any, List, Optional

from waflib import Logs
from waflib.Build import BuildContext
from waflib.Node import Node

# Root of lib-common, from the real path of this file: the projects that
# use lib-common as a sub-module link this file in their own waftools.
LIB_COMMON_ROOT = osp.dirname(
    osp.dirname(osp.dirname(osp.realpath(__file__)))
)


def get_excluded_node(ctx: BuildContext) -> Optional[Node]:
    """
    Return the directory whose DSOs `iop-pystubs` must not rebuild.

    It is the test directory of lib-common (`test-iop-plugin`,
    `test-iop-plugin2`) when lib-common is a sub-module: these test
    fixtures set `pystub_path=` to keep their stubs in sync, but the
    project does not use them. When lib-common is the project, its
    tests are its own, so nothing is excluded.
    """
    lib_common_node = ctx.root.find_dir(LIB_COMMON_ROOT)
    if lib_common_node is None or lib_common_node is ctx.srcnode:
        return None
    return lib_common_node.find_dir('tests')


def collect_pystub_tgens(ctx: BuildContext) -> List[Any]:
    """
    Return every task generator whose shlib has `pystub_path=` set,
    excluding the test fixtures of lib-common (see
    `get_excluded_node()`), in stable discovery order.
    """
    excluded_node = get_excluded_node(ctx)
    seen = set()
    tgens = []
    for grp in ctx.groups:
        for tgen in grp:
            if not getattr(tgen, 'pystub_path', None):
                continue
            target = getattr(tgen, 'target', None)
            if not target or target in seen:
                continue
            if excluded_node is not None and tgen.path.is_child_of(
                excluded_node
            ):
                continue
            seen.add(target)
            tgens.append(tgen)
    return tgens


def drop_companion_depends_on(tgens: List[Any]) -> List[str]:
    """
    Clear the `depends_on` of each pystub DSO task generator.

    A pystub DSO only needs to be *linked* (from its `use` closure) so
    that `make_dso_pystub.py` can load it and introspect its IOP
    packages. Its `depends_on` entries are runtime/CI companions (full
    applications and test binaries such as `mcms`, `igloo-ci`,
    `rpcgsm-*`, `intersec`, ...) that are NOT linked into the DSO.
    `post_depends_on()` would nonetheless `post()` them — dragging their
    whole implementation closure (e.g. `services-cerbere`/`cerbere.blk`)
    into the build for nothing.

    We only touch the DSO targets themselves, never their `use` closure,
    so the `depends_on` ordering of the libraries that are genuinely
    built (generated headers, version stamps, ...) is left intact.

    Returns the names of the targets whose `depends_on` was cleared (for
    logging).
    """
    cleared = []
    for tgen in tgens:
        dropped = list(tgen.to_list(getattr(tgen, 'depends_on', [])))
        if dropped:
            tgen.depends_on = []
            cleared.append(f'{tgen.target} (was: {", ".join(dropped)})')
    return cleared


def narrow_to_pystub_targets(ctx: BuildContext) -> None:
    """
    pre-build hook for the `iop-pystubs` command: scope the build
    to the DSO targets that produce a `*__iop.pyi` stub.
    """
    if ctx.cmd != 'iop-pystubs':
        return

    tgens = collect_pystub_tgens(ctx)
    if not tgens:
        Logs.warn(
            'iop-pystubs: no task generator with pystub_path= found; '
            'nothing to do'
        )
        ctx.groups = []
        return

    targets = [tgen.target for tgen in tgens]

    Logs.info(
        'iop-pystubs: (re)generating Python IOP stubs for %d DSO target(s):',
        len(targets),
    )
    for target in targets:
        Logs.info('  - %s', target)

    # Drop the DSOs' `depends_on` companions: they are not linked into
    # the DSO and not needed to introspect its IOP packages, but would
    # otherwise pull their whole implementation closure into the build.
    cleared = drop_companion_depends_on(tgens)
    if cleared:
        Logs.info(
            'iop-pystubs: ignoring depends_on of %d target(s) '
            '(not needed for stub generation):',
            len(cleared),
        )
        for line in cleared:
            Logs.info('  - %s', line)

    # `Build.compile()` and `post_group()` both honour `self.targets` to
    # restrict which task generators get posted. Setting it here (during
    # the pre_build phase, after `recurse()` has populated `ctx.groups`)
    # is enough to scope the upcoming compile to the chosen DSOs and
    # their transitive dependencies.
    ctx.targets = ','.join(targets)


class IopPystubs(BuildContext):
    """build every IOP DSO (re-generates the matching *__iop.pyi stubs)"""

    cmd = 'iop-pystubs'


def build(ctx: Any) -> None:
    ctx.add_pre_fun(narrow_to_pystub_targets)
