#!/usr/bin/env python3
# -*- coding: utf-8 -*-
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

import binascii
import datetime
import os
import struct

from enum import IntEnum
from optparse import OptionParser

CORPUS_DIR = "corpus"
CORPUS_NAME = "corpus-init"
CORPUS_NBR = 0
CORPUS_DICT = dict()


class QpsstressStep(IntEnum):
    """
    Class used to identify QPS stress steps in this script and recreate
    fuzzing binary blobs and create a dictionary for efficient fuzzing.

    Any changes in the qpsstress_step_t enumeration MUST BE applied here.
    """
    QPS_ALLOC = 0
    QPS_REALLOC = 1
    QPS_DEALLOC = 2
    QPS_WDEREF = 3
    QPS_SNAPSHOT = 4
    QPS_SNAPSHOT_WAIT = 5
    QPS_REOPEN = 6

    QPS_OBJ_CREATE = 7
    QPS_OBJ_SET = 8
    QPS_OBJ_MULTIPLE_SET = 9
    QPS_OBJ_GET = 10
    QPS_OBJ_MULTIPLE_GET = 11
    QPS_OBJ_RM_ENTRY = 12
    QPS_OBJ_MULTIPLE_RM_ENTRY = 13
    QPS_OBJ_CLEAR = 14
    QPS_OBJ_DELETE = 15

    QPS_HAT_RESET_ENUMERATOR = 16
    QPS_HAT_MV_ENUMERATOR = 17
    QPS_HAT_MV_ENUMERATOR_TO = 18
    QPS_HAT_CHECK_ENUMERATOR = 19
    QPS_HAT_SET_ENUMERATOR = 20
    QPS_HAT_COMPUTE_COUNTS = 21
    QPS_HAT_COMPUTE_MEMORY = 22
    QPS_HAT_FIX_STORED0 = 23

    QPS_BITMAP_COMPUTE_STATS = 24


class QpsstressObj(IntEnum):
    """
    Class used to identify QPS stress objects (like QpsstressStep class for
    QPS stress steps).

    Any changes in the qps_obj_type_t enumeration MUST BE applied here.
    """
    QPS_GENERIC_TYPE = 0
    QPS_QHAT = 1
    QPS_QBITMAP = 2


class FuzzingStep:
    """
    Convert a basic fuzzing step into a binary blob used for fuzzing
    operations.
    """
    def __init__(self, step, handle=None):
        self.step = step
        self.handle = handle
        # This size is the maximum size expected for a fuzzing binary blob.
        self.blob_size_expected = 13
        self.fuzz_dict = dict()

    def pack_blob(self):
        """
        Create binary structure associated with the python object. Unlike the
        pack() method, this method should be overloaded to represent the
        fuzzing object.
        """
        if self.handle is None:
            self.handle = 0
        # Fuzzing starting always with the fuzzing step (1 byte) then handle
        # (4 bytes).
        return struct.pack('<BI', self.step, self.handle)

    def pack(self):
        """
        Pack the fuzzing blob and ensures its size is self.blob_size_expected
        (this is useful to easily iterate through steps in the qpsstress
        program).
        """
        blob = self.pack_blob()

        if len(blob) < self.blob_size_expected:
            blob += bytes([0] * (self.blob_size_expected - len(blob)))
        return blob


class FuzzingGenericStep(FuzzingStep):
    """
    Convert a QPS step (like QPS alloc, QPS realloc) into a binary blob used
    for fuzzing operations.
    """
    def __init__(self, step, handle=None, size=None):
        super().__init__(step, handle=handle)
        self.size = size

    def pack_blob(self):
        if self.size is None:
            self.size = 0
        return super().pack_blob() + struct.pack('<I', self.size)


class FuzzingQpsObjStep(FuzzingStep):
    """
    Convert a QPS object step (like QPS hat check/set/reset enumerator,
    QPS hat fix stored 0) into a binary blob used for fuzzing operations.
    """
    def __init__(self, step, handle=0, type_obj=QpsstressObj.QPS_QHAT):
        super().__init__(step, handle=handle)
        self.type_obj = type_obj

    def pack_blob(self):
        return super().pack_blob() + struct.pack('<B', self.type_obj)


class FuzzingQpsObjStepWithKey(FuzzingQpsObjStep):
    """
    Convert a QPS object step using keys (like QPS hat set key) into a binary
    blob used for fuzzing operations.
    """
    def __init__(self, step, handle=0, type_obj=QpsstressObj.QPS_QHAT,
                 key=1):
        super().__init__(step, handle=handle)
        self.type_obj = type_obj
        self.key = key

    def pack_blob(self):
        return super().pack_blob() + struct.pack('<I', self.key)


class FuzzingQpsObjMultipleOp(FuzzingQpsObjStep):
    """
    Convert a QPS object step using multiples QPS operations into a binary
    blob used for fuzzing operations.

    Multiple operations fuzzed on QPS in one step have the advantage of
    simulating a better entropy on QPS operations without the hassle to waste
    time in decoding each step in the fuzzing blob. Number of iterations is
    limited by an uint16 value (UINT16_MAX).
    """
    def __init__(self, step, handle=0, type_obj=QpsstressObj.QPS_QHAT, key=1,
                 nbr_iter=2, gap_keys=1):
        super().__init__(step, handle=handle, type_obj=type_obj)
        self.key = key
        self.nbr_iter = nbr_iter
        self.gap_keys = gap_keys

    def pack_blob(self):
        return super().pack_blob() + struct.pack('<IHB', self.key,
                                                 self.nbr_iter, self.gap_keys)


class FuzzingQhatObjStepMv(FuzzingQpsObjStep):
    """
    Convert a dedicated Qhat enumerator move step into a binary blob used for
    fuzzing operations.
    """
    def __init__(self, step, handle=0, move_count=3):
        super().__init__(step, handle=handle, type_obj=QpsstressObj.QPS_QHAT)
        self.move_count = move_count

    def pack_blob(self):
        return super().pack_blob() + struct.pack('<H', self.move_count)


class FuzzingQhatCompute(FuzzingQpsObjStep):
    """
    Convert a dedicated Qhat compute step into a binary blob used for fuzzing
    operations.
    """
    def __init__(self, step, handle=0, do_stats=None, do_mem_overhead=None):
        super().__init__(step, handle=handle, type_obj=QpsstressObj.QPS_QHAT)

        if do_stats is not None:
            self.flag = do_stats
        elif do_mem_overhead is not None:
            self.flag = do_mem_overhead
        else:
            raise Exception("do_stats or do_mem_overhead expected")

    def pack_blob(self):
        return super().pack_blob() + struct.pack('<B', self.flag)


class FuzzingQpsObjStepCreate(FuzzingQpsObjStep):
    """
    Convert a dedicated Qhat create step into a binary blob used for fuzzing
    operations.
    """
    def __init__(self, step, type_obj=QpsstressObj.QPS_QHAT,
                 is_nullable=False, value_len=4):
        super().__init__(step, type_obj=type_obj)
        self.is_nullable = is_nullable
        self.value_len = value_len

    def pack_blob(self):
        val_is_nullable = 1 if self.is_nullable else 0
        return super().pack_blob() + struct.pack('<BB', val_is_nullable,
                                                 self.value_len)


def create_corpus_files_and_dict(corpus, generate_files=False,
                                 fuzz_dict_name=None):
    """
    Based on the list of sequences representing themselves a list of steps,
    create a list of files for the initial corpus (initial stimulation
    entry point) and/or the dictionary used to represent steps for fuzzing
    operations.
    """
    corpus_dict = dict()
    f = None

    assert generate_files or fuzz_dict_name is not None
    try:
        for i, corpus_case in enumerate(corpus):
            # If we want to generate files containing a fuzzing sequence,
            # create it now.
            if generate_files:
                f = open(os.path.join(CORPUS_DIR, f"{CORPUS_NAME}-{i}.bin"),
                         mode='wb')

            if not isinstance(corpus_case, list):
                corpus_case = [corpus_case]

            # Write for one sequence each step.
            for item in corpus_case:
                blob = item.pack()
                if fuzz_dict_name:
                    # If a fuzzing dictionary is requested, register this
                    # unique case (the dict key is itself the binary step).
                    corpus_dict[blob] = None
                if f:
                    # If a fuzzing sequence is requested, write this step
                    # in the file describing the fuzzing sequence.
                    f.write(blob)
            if f:
                f.close()

    except Exception as e:
        raise e

    if corpus_dict and fuzz_dict_name:
        with open(fuzz_dict_name, "w") as f:
            # Write the dictionary using the "dedicated" format.
            # Add time of generation, so we have less doubt about the fact
            # that we need to generate it again or not.
            now = datetime.datetime.now()
            f.write("# Generated at " + now.strftime("%Y-%m-%d %H:%M:%S\n\n"))
            for i, k in enumerate(sorted(corpus_dict.keys())):
                blob_str = str(binascii.hexlify(k))[2:-1]
                fuzz_key = '\\x'
                fuzz_key += '\\x'.join([(blob_str[i:i + 2])
                                        for i in range(0, len(blob_str), 2)])
                f.write(f'kw{i + 1}="' + fuzz_key + '"\n')
            f.write('\n')


def create_corpus(with_snapshot_reopen=False, generate_files=True,
                  fuzz_dict_name=None):
    max_mem_alloc = 33554431
    corpus = []

    corpus += [[
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=24),
        FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=65 * 1024),
        FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=2),
        FuzzingGenericStep(QpsstressStep.QPS_REALLOC, size=65 * 1024,
                           handle=1),
        FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
        FuzzingGenericStep(QpsstressStep.QPS_REALLOC, size=24, handle=2),
        FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=2),
        FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=1),
        FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=2),
    ]]

    # Use the possibility to enable or disable QPS snapshots and reopen, as it
    # kills the number of fuzzing executions per second (and we are
    # supposed to have 1000 exec per second for efficient fuzzing).
    if with_snapshot_reopen:
        corpus += [[
            # Allocate memory in TLSF map.
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=24),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
            # Allocate memory in QPS page map.
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=65 * 1024),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=2),
            FuzzingGenericStep(QpsstressStep.QPS_REALLOC, size=65 * 1024),
            FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=1),
            FuzzingGenericStep(QpsstressStep.QPS_SNAPSHOT),
            FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=2),
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=0),
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=0),
            FuzzingGenericStep(QpsstressStep.QPS_SNAPSHOT_WAIT),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
        ]]

        corpus += [[
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=24),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=65 * 1024),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=2),
            FuzzingGenericStep(QpsstressStep.QPS_REALLOC, size=65 * 1024),
            FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=1),
            FuzzingGenericStep(QpsstressStep.QPS_SNAPSHOT),
            FuzzingGenericStep(QpsstressStep.QPS_DEALLOC, handle=2),
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=0),
            FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=0),
            FuzzingGenericStep(QpsstressStep.QPS_REOPEN),
            FuzzingGenericStep(QpsstressStep.QPS_WDEREF, handle=1),
        ]]

    # Sequence playing with multiple QPS maps, improves coverage.
    corpus += [[
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=10),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
        FuzzingGenericStep(QpsstressStep.QPS_ALLOC, size=max_mem_alloc),
    ]]

    corpus += [[
        # Sequence playing with Qhat objects.
        FuzzingQpsObjStepCreate(QpsstressStep.QPS_OBJ_CREATE,
                                type_obj=QpsstressObj.QPS_QHAT,
                                is_nullable=False, value_len=4),
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_OBJ_SET, handle=1,
                                 type_obj=QpsstressObj.QPS_QHAT, key=10),
        FuzzingQpsObjMultipleOp(QpsstressStep.QPS_OBJ_MULTIPLE_SET, handle=1,
                                type_obj=QpsstressObj.QPS_QHAT, key=20,
                                nbr_iter=2, gap_keys=10),
        FuzzingQhatObjStepMv(QpsstressStep.QPS_HAT_MV_ENUMERATOR, handle=1,
                             move_count=1),
        # "move enumerator to" action must pick a key bigger or equal to the
        # current key from the enumerator, so >= 20 in this corpus
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_HAT_MV_ENUMERATOR_TO,
                                 handle=1, type_obj=QpsstressObj.QPS_QHAT,
                                 key=30),
        FuzzingQpsObjStep(QpsstressStep.QPS_HAT_CHECK_ENUMERATOR, handle=1),
        FuzzingQpsObjStep(QpsstressStep.QPS_HAT_SET_ENUMERATOR, handle=1),
        FuzzingQpsObjStep(QpsstressStep.QPS_HAT_RESET_ENUMERATOR, handle=1),
        FuzzingQpsObjStep(QpsstressStep.QPS_HAT_FIX_STORED0, handle=1),
        FuzzingQhatCompute(QpsstressStep.QPS_HAT_COMPUTE_COUNTS,
                           handle=1, do_stats=0),
        FuzzingQhatCompute(QpsstressStep.QPS_HAT_COMPUTE_MEMORY,
                           handle=1, do_mem_overhead=0),
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_OBJ_RM_ENTRY, handle=1,
                                 type_obj=QpsstressObj.QPS_QHAT, key=10),
        FuzzingQpsObjMultipleOp(QpsstressStep.QPS_OBJ_MULTIPLE_RM_ENTRY,
                                handle=1, type_obj=QpsstressObj.QPS_QHAT,
                                key=20, nbr_iter=2, gap_keys=10),
        FuzzingQpsObjStep(QpsstressStep.QPS_OBJ_CLEAR, handle=1,
                          type_obj=QpsstressObj.QPS_QHAT),
        FuzzingQpsObjStep(QpsstressStep.QPS_OBJ_DELETE, handle=1,
                          type_obj=QpsstressObj.QPS_QHAT),
    ]]

    corpus += [[
        # Sequence playing with Qbitmap objects.
        FuzzingQpsObjStepCreate(QpsstressStep.QPS_OBJ_CREATE,
                                type_obj=QpsstressObj.QPS_QBITMAP,
                                is_nullable=False, value_len=4),
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_OBJ_SET, handle=1,
                                 type_obj=QpsstressObj.QPS_QBITMAP, key=10),
        FuzzingQpsObjMultipleOp(QpsstressStep.QPS_OBJ_MULTIPLE_SET, handle=1,
                                type_obj=QpsstressObj.QPS_QBITMAP, key=20,
                                nbr_iter=2, gap_keys=10),
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_OBJ_GET, handle=1,
                                 type_obj=QpsstressObj.QPS_QBITMAP, key=20),
        FuzzingQpsObjStep(QpsstressStep.QPS_BITMAP_COMPUTE_STATS, handle=1,
                          type_obj=QpsstressObj.QPS_QBITMAP),
        FuzzingQpsObjStepWithKey(QpsstressStep.QPS_OBJ_RM_ENTRY, handle=1,
                                 type_obj=QpsstressObj.QPS_QBITMAP, key=10),
        FuzzingQpsObjMultipleOp(QpsstressStep.QPS_OBJ_MULTIPLE_RM_ENTRY,
                                handle=1, type_obj=QpsstressObj.QPS_QBITMAP,
                                key=20, nbr_iter=2, gap_keys=10),
        FuzzingQpsObjStep(QpsstressStep.QPS_OBJ_CLEAR, handle=1,
                          type_obj=QpsstressObj.QPS_QBITMAP),
        FuzzingQpsObjStep(QpsstressStep.QPS_OBJ_DELETE, handle=1,
                          type_obj=QpsstressObj.QPS_QBITMAP),
    ]]

    # Generate files from all sequences provided before.
    create_corpus_files_and_dict(corpus, generate_files=generate_files,
                                 fuzz_dict_name=fuzz_dict_name)


if __name__ == '__main__':
    ALL_OPTS = OptionParser()
    ALL_OPTS.add_option("-d", "--dict-file", dest="fuzz_dict_name",
                        action=None, help="export fuzzing dict to FILE",
                        metavar="FILE")
    ALL_OPTS.add_option("-g", "--generate-corpus",
                        action="store_true", dest="generate_files",
                        default=False,
                        help="Define if all corpus steps should be written "
                             "for libFuzzer")
    ALL_OPTS.add_option("-s", "--slow-runs",
                        action="store_true", dest="slow_runs",
                        default=False,
                        help="Define if all steps like snapshot, reopen "
                             "must be triggered for libFuzzer")

    (CREATE_OPTS, CREATE_ARGS) = ALL_OPTS.parse_args()

    if CREATE_OPTS.fuzz_dict_name is None and not CREATE_OPTS.generate_files:
        raise RuntimeError('Need at least -g or -d, see the help (-h)')

    if CREATE_OPTS.generate_files and not os.path.isdir(CORPUS_DIR):
        raise RuntimeError(f'Create first folder "{CORPUS_DIR}" before -g '
                           f'option')

    create_corpus(with_snapshot_reopen=CREATE_OPTS.slow_runs,
                  generate_files=CREATE_OPTS.generate_files,
                  fuzz_dict_name=CREATE_OPTS.fuzz_dict_name)
