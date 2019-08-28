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

import re

# {{{ smidump object parsing

def parse_description(obj):
    obj['desc'] = ""
    obj['criticity'] = ""
    obj['expected'] = ""

    if 'description' not in obj:
        return

    m = re.match("[^@]*@criticity ([^\n]*)[^@]*", obj['description'])
    if m:
        obj['criticity'] = m.group(1).strip("\n ")

    m = re.match("[^@]*@expected ([^\n]*)[^@]*", obj['description'])
    if m:
        obj['expected'] = m.group(1).strip("\n ")

    m = re.match("([^@]*).*", obj['description'])
    if m:
        obj['desc'] = m.group(1).strip("\n ")

def build_tree_(tree, mib):
    for key in mib:
        oid = mib[key]['oid'].split('.')
        leaf = tree
        for l in oid:
            if 'leaf' not in leaf:
                leaf['leaf'] = {}
            leaf = leaf['leaf']
            if l not in leaf:
                leaf[l] = {}
            leaf = leaf[l]
        leaf['key'] = key
        leaf['obj'] = mib[key]
        parse_description(leaf['obj'])

def build_fullname(tree, base):
    fullname = ''
    if 'key' in tree:
        if base == '':
            fullname = tree['key']
        else:
            fullname = base + '.' + tree['key']

        tree['fullname'] = fullname
        tree['depth'] = len(fullname.split('.'))

    if 'leaf' in tree:
        for l in tree['leaf']:
            build_fullname(tree['leaf'][l], fullname)

def build_tree(mib):
    tree = {}
    build_tree_(tree, mib['nodes'])
    build_tree_(tree, mib['notifications'])
    build_fullname(tree, '')
    return tree

# }}}
# {{{ description rendering

def gen_filename():
    path = FILENAME.split('/') #pylint: disable=E0602
    return path[len(path) - 1].split('.')[0]

def gen_title(tree, is_leaf=False):
    line = ""
    if is_leaf:
        line += "[[%s_%s]]\n" % (gen_filename(), tree['key'])
    line += ''.ljust(1 + min(tree['depth'], 2), "=")
    line += ' ' + tree['key'] + "\n\n"
    return line

def gen_node(tree):
    if tree['depth'] == 1:
        return gen_title(tree)
    else:
        return ''

# {{{ notifications

def gen_notification(tree):
    line = gen_title(tree, True)

    line += "[cols='1s,3']\n"
    line += "|===\n"
    line += "|Name\n"
    line += "|%s\n" % tree['key']
    line += "|OID\n"
    line += "|%s\n" % tree['obj']['oid']
    line += "\n"
    line += "|Description\n"
    line += "|%s\n" % tree['obj']['desc']
    line += "\n"
    line += "|Criticity\n"
    line += "|%s\n" % tree['obj']['criticity']
    line += "\n"

    if 'objects' in tree['obj']:
        line += "|Arguments\n"
        if len(tree['obj']['objects']):
            line += "a|[options='compact']\n"
            for arg in tree['obj']['objects']:
                line += '* <<%s_%s, %s>>\n' % (gen_filename(), arg, arg)
        else:
            line += "|"
        line += '\n'

    line += "|===\n"
    line += '\n'
    return line

def gen_notifications(tree):
    node = ''
    res = ''

    if 'obj' in tree:
        if tree['obj']['nodetype'] == 'notification':
            return gen_notification(tree)
        elif tree['obj']['nodetype'] != 'node':
            return ''

        node += gen_node(tree)

    if 'leaf' not in tree:
        return ''
    for l in tree['leaf']:
        res += gen_notifications(tree['leaf'][l])

    if res != '':
        return node + res
    return ''

# }}}
# {{{ objects

def gen_object(tree):
    line = gen_title(tree, True)

    line += "[cols='1s,3']\n"
    line += "|===\n"
    line += "|Name\n"
    line += "|%s\n" % tree['key']
    line += "|OID\n"
    line += "|%s\n" % tree['obj']['oid']
    line += "\n"
    line += "|Description\n"
    line += "|%s\n" % tree['obj']['desc']
    line += "\n"
    line += "|Expected\n"
    line += "|%s\n" % tree['obj']['expected']
    line += "\n"
    line += "|===\n"
    line += '\n'
    return line

def gen_objects(tree):
    node = ''
    res = ''

    if 'obj' in tree:
        if tree['obj']['nodetype'] == 'scalar':
            return gen_object(tree)
        elif tree['obj']['nodetype'] != 'node':
            return ''

        node += gen_node(tree)

    if 'leaf' not in tree:
        return ''
    for l in tree['leaf']:
        res += gen_objects(tree['leaf'][l])

    if res != '':
        return node + res
    return ''

# }}}
# }}}

if __name__ == '__main__':
    BTREE = build_tree(MIB) #pylint: disable=E0602

    print 'ifdef::notifications[]\n'
    print gen_notifications(BTREE)
    print 'endif::notifications[]\n'
    print '\n'
    print 'ifdef::objects[]\n'
    print gen_objects(BTREE)
    print 'endif::objects[]\n'
