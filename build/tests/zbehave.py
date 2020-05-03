#!/usr/bin/env python3
#vim:set fileencoding=utf-8:
###########################################################################
#                                                                         #
# Copyright 2020 INTERSEC SA                                              #
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


import sys
import os
import behave.model
import zpycore.util

from behave.formatter.base import Formatter
from behave.__main__ import main as behave_main

try:
    from behave.formatter._registry import register as behave_register
except ImportError:
    from behave.formatter.formatters import register as behave_register


class ZFormatter(Formatter):
    """
    Provide a behave formatter that support the z format
    """

    status = {
        "passed":    "pass",
        "failed":    "fail",
        "error":     "fail",
        "skipped":   "skip",
        "untested":  "skip",
        "undefined": "fail",
    }

    name = "z"

    step_tpl = "# {0:>2}-{1:<2} {2} {3} {4}:{5:<3}   # ({6:>.3f}s)\n"

    def __init__(self, stream, config):
        # Force show_skipped to order the formatter to be called for skipped
        # features.
        config.show_skipped = True
        super().__init__(stream, config)
        self.reset()

    def reset(self):
        self.__count = -1
        self.__success = 0
        self.__skipped = 0
        self.__failed  = 0
        self.__scenario = None
        self.__status   = None
        self.__exn      = None
        self.steps = []
        self.basename = ""

    def scenario_flush(self):
        zpycore.util.wipe_children_rearm()
        if self.__scenario is not None:
            if self.__status == "pass":
                self.__success += 1
            elif self.__status == "fail":
                self.__failed += 1
            elif self.__status == "skip":
                self.__skipped += 1
            self.stream.write("%d %s %s   # (%.3fs) %d steps\n" %
                              (self.__count, self.__status,
                               self.__scenario.name, self.__scenario.duration,
                               self.__steps))
            self.stream.flush()
            if self.__exn:
                for line in self.__exn.split('\n'):
                    print(":", line)
            self.__count += 1

        # Print only skipped steps on running scenario (only in case of error)
        if len(self.steps) > 0 and self.__status != "skip":
            self.stream.write(": Steps skipped:\n")
            for step in self.steps:
                self.stream.write(": %s\n" % step)
            self.stream.flush()

        self.steps = []
        self.__scenario = None
        self.__steps    = 0
        self.__status   = None
        self.__exn      = None

    def feature(self, feature):
        self.basename = os.path.basename(feature.filename)
        self.scenario_flush()
        count = 0
        for sc in feature.scenarios:
            if isinstance(sc, behave.model.ScenarioOutline):
                count += len(sc.scenarios)
            else:
                count += 1
        self.stream.write("1..%d %s\n" % (count, feature.name))
        self.stream.flush()
        self.__count = 1
        self.__steps = 0

    def scenario(self, scenario):
        self.scenario_flush()
        self.__scenario = scenario
        self.__status = "skip"

    def result(self, step_result):
        self.__steps += 1

        status = step_result.status
        if not isinstance(status, str):
            status = status.name

        status = self.status.get(status, "skip")

        if self.__status == "skip":
            self.__status = status

        step = self.steps.pop(0)
        step_name = "<{0} {1}>".format(step.step_type, step.name)
        self.stream.write(self.step_tpl.format(
            self.__count, self.__steps, status,
            step_name, self.basename, step.line, step.duration))
        self.stream.flush()
        if status == "fail":
            self.__exn    = step_result.error_message
            self.__status = "fail"

    def step(self, step):
        self.steps.append(step)

    def eof(self):
        self.scenario_flush()
        if self.__count != 1:
            total = self.__success + self.__skipped + self.__failed
            self.stream.write(
                "# %.1f%% skipped  %.1f%% passed  %.1f%% failed\n" %
                ((100. * self.__skipped) / total,
                 (100. * self.__success) / total,
                 (100. * self.__failed)  / total))
            self.stream.flush()

        self.reset()


def run_behave():
    with zpycore.util.wipe_children_register():
        behave_register(ZFormatter)
        sys.exit(behave_main())


if __name__ == '__main__':
    run_behave()
