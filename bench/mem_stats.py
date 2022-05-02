#!/usr/bin/env python3
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

""" Utility to read mem-bench logs.

Takes the log file as first argument.
It plots the used memory/allocated memory graph.
"""

import numpy as np
import matplotlib.pyplot as plt
import sys

COLUMNS = [
    # alloc block
    "alloc_nb", "alloc_slow",
    "alloc_nbtimer", "alloc_min", "alloc_max",
    "alloc_tot",
    # realloc block
    "realloc_nb", "realloc_slow",
    "realloc_nbtimer", "realloc_min", "realloc_max",
    "realloc_tot",
    # free block
    "free_nb", "free_slow",
    "free_nbtimer", "free_min", "free_max",
    "free_tot",
    # memory block
    "total_allocated", "total_requested",
    "max_allocated", "max_unused", "max_used",
    "malloc_calls", "current_used", "current_allocated"
]
POSITION = dict((s, i) for (i, s) in enumerate(COLUMNS))
NUM_COLUMNS = len(COLUMNS)


def plot(filename):
    data = np.loadtxt(filename, delimiter=',') #pylint: disable=E1101
    print(data.shape)
    print(NUM_COLUMNS)

    # adding "time"
    time = np.array(range(len(data[:,0]))) #pylint: disable=E1101

    # generate figure
    (_, ax1) = plt.subplots()
    ax1.plot(time, data[:,POSITION["current_used"]] / (1024 * 1024))
    ax1.plot(time, data[:,POSITION["current_allocated"]] / (1024 * 1024))
    ax1.set_xlabel('saves')
    ax1.set_ylabel('memory (MB)')
    ax1.legend(('Used memory', 'Available memory'), loc=0)
    ax2 = ax1.twinx()
    ax2.plot(time, data[:,POSITION["malloc_calls"]], 'r')
    ax2.set_ylabel('malloc calls', color='r')
    plt.show()



FILENAME = sys.argv[1]
if __name__ == "__main__":
    plot(FILENAME)
