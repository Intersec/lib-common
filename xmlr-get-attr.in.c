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

__flatten
int F(xmlr_getattr)(xml_reader_t xr, xmlAttrPtr attr, ARGS_P)
{
    const char *name = (const char *)attr->name;
    xmlNodePtr  n    = attr->children;

    assert (xmlr_on_element(xr, false));

    if (n == NULL)
        return F(xmlr_attr)(xr, name, NULL, ARGS);
    if (n->next == NULL
    &&  (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE))
    {
        return F(xmlr_attr)(xr, name, (const char *)n->content, ARGS);
    } else {
        char *s = (char *)xmlNodeListGetString(attr->doc, n, 1);
        int res = F(xmlr_attr)(xr, name, s, ARGS);

        xmlFree(s);
        return res;
    }
}

#undef F
#undef ARGS
#undef ARGS_P
