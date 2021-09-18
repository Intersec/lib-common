/***************************************************************************/
/*                                                                         */
/* Copyright 2021 INTERSEC SA                                              */
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

#include <lib-common/xmlpp.h>
#include <lib-common/z.h>

Z_GROUP_EXPORT(xmlpp) {
    Z_TEST(xmlpp_tag_scope, "xmlpp_tag_scope") {
        xmlpp_t pp;
        SB_8k(xml1);
        SB_8k(xml2);

        xmlpp_open_banner(&pp, &xml1);
        xmlpp_opentag(&pp, "level1");
        xmlpp_opentag(&pp, "level2");
        xmlpp_putattr(&pp, "attr", "foo");
        xmlpp_closetag(&pp);
        xmlpp_closetag(&pp);
        xmlpp_close(&pp);

        xmlpp_open_banner(&pp, &xml2);
        xmlpp_tag_scope(&pp, "level1") {
            xmlpp_tag_scope(&pp, "level2") {
                xmlpp_putattr(&pp, "attr", "foo");
            }
        }
        xmlpp_close(&pp);

        Z_ASSERT_STREQUAL(xml1.data, xml2.data,
                          "xml created with xmlpp_opentag/xmlpp_closetag "
                          "or xmlpp_tag_scope should be the same");
    } Z_TEST_END;
} Z_GROUP_END;
