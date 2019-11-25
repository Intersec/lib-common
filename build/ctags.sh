#!/bin/sh
###########################################################################
#                                                                         #
# Copyright 2019 INTERSEC SA                                              #
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

TAGSOPTION="$1"
TAGSOUTPUT="$2"

ctags ${TAGSOPTION} -o ${TAGSOUTPUT} --recurse=yes --totals=yes --links=no \
    --c-kinds=+p --c++-kinds=+p --fields=+liaS --extra=+q \
    \
    --langmap=c:+.blk --langmap=c:+.h --langmap=c++:+.blkk \
    \
    -I 'qv_t qm_t qh_t IOP_RPC_IMPL IOP_RPC_CB qvector_t qhp_min_t qhp_max_t MODULE_BEGIN MODULE_END MODULE_DEPENDS_ON+ MODULE_NEEDED_BY+' \
    --regex-c='/^OBJ_CLASS(_NO_TYPEDEF)?\(+([^,]+),/\2_t/o, cclass/' \
    --regex-c='/^    .*\(\*+([^\ ]+)\)\([a-zA-Z_]+ /\1/x, cmethod/' \
    --regex-c='/^qvector_t\(+([a-zA-Z_]+)\,/\1/t, qvector/' \
    --regex-c='/^q[hm]_k.*_t\(+([a-zA-Z_]+)/\1/t, qhash/' \
    --regex-c='/^MODULE_BEGIN\(+([a-zA-Z_]+)/\1/m, module/' \
    \
    --langdef=iop --langmap=iop:.iop \
    --regex-iop='/^struct +([a-zA-Z]+)/\1/s, struct/' \
    --regex-iop='/^(local +)?(abstract +)?(local +)?class +([a-zA-Z]+)/\4/c, class/' \
    --regex-iop='/^union +([a-zA-Z]+)/\1/u, union/' \
    --regex-iop='/^enum +([a-zA-Z]+)/\1/e, enum/' \
    --regex-iop='/^typedef +[^;]+ +([a-zA-Z]+) *;/\1/t, typedef/' \
    --regex-iop='/^\@ctype\(+([a-zA-Z_]+)\)/\1/t, ctype/' \
    --regex-iop='/^interface +([a-zA-Z]+)/\1/n, interface/' \
    --regex-iop='/^snmpIface +([a-zA-Z]+)/\1/s, snmpiface/' \
    --regex-iop='/^snmpObj +([a-zA-Z]+)/\1/s, snmpobj/' \
    --regex-iop='/^snmpTbl +([a-zA-Z]+)/\1/s, snmptbl/' \
    \
    --langmap=javascript:+.jsx \
    --langdef=typescript \
    --langmap=typescript:+.ts \
    --langmap=typescript:+.tsx \
    --regex-typescript='/^[ \t]*(export[ \t]+([a-z]+[ \t]+)?)?class[ \t]+([a-zA-Z0-9_$]+)/\3/c,classes/' \
    --regex-typescript='/^[ \t]*(declare[ \t]+)?namespace[ \t]+([a-zA-Z0-9_$]+)/\2/c,modules/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?module[ \t]+([a-zA-Z0-9_$]+)/\2/n,modules/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?(async[ \t]+)?function[ \t]+([a-zA-Z0-9_$]+)/\3/f,functions/' \
    --regex-typescript='/^[ \t]*export[ \t]+(var|let|const)[ \t]+([a-zA-Z0-9_$]+)/\2/v,variables/' \
    --regex-typescript='/^[ \t]*(var|let|const)[ \t]+([a-zA-Z0-9_$]+)[ \t]*=[ \t]*function[ \t]*[*]?[ \t]*\(\)/\2/v,varlambdas/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?(public|protected|private)[ \t]+(static[ \t]+)?(abstract[ \t]+)?(((get|set)[ \t]+)|(async[ \t]+[*]*[ \t]*))?([a-zA-Z1-9_$]+)/\9/m,members/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?interface[ \t]+([a-zA-Z0-9_$]+)/\2/i,interfaces/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?type[ \t]+([a-zA-Z0-9_$]+)/\2/t,types/' \
    --regex-typescript='/^[ \t]*(export[ \t]+)?enum[ \t]+([a-zA-Z0-9_$]+)/\2/e,enums/' \
    --regex-typescript='/^[ \t]*import[ \t]+([a-zA-Z0-9_$]+)/\1/I,imports/' \
    \
    --exclude=".build*" --exclude=".git" \
    --exclude="*.blk.c" --exclude="*.blkk.cc" \
    --exclude="js/v8" --exclude="ext" --exclude="node_modules/*" \
    \
    --languages='c,c++,iop,python,php,javascript,typescript'
