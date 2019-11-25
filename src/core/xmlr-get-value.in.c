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

#ifndef PRE_ARGS_P
#define PRE_ARGS_P
#endif

__flatten
int F(xmlr_get)(PRE_ARGS_P xml_reader_t xr, ARGS_P)
{
    assert (xmlr_on_element(xr, false));

    if (xmlTextReaderIsEmptyElement(xr) == 1) {
        RETHROW(F(xmlr_val)(xr, NULL, ARGS));
        return xmlr_next_node(xr);
    }

    if (RETHROW(xmlTextReaderRead(xr)) != 1)
        return xmlr_fail(xr, "expecting text or closing element");
    RETHROW(xmlr_scan_node(xr, true));
    if (xmlTextReaderNodeType(xr) == XTYPE(TEXT)) {
        const char *s = (const char *)xmlTextReaderConstValue(xr);

        RETHROW(F(xmlr_val)(xr, s, ARGS));
        RETHROW(xmlr_scan_node(xr, false));
    } else {
        RETHROW(F(xmlr_val)(xr, NULL, ARGS));
    }
    if (xmlTextReaderNodeType(xr) != XTYPE(END_ELEMENT))
        return xmlr_fail(xr, "expecting closing tag");

    return xmlr_next_node(xr);
}

#undef F
#undef ARGS
#undef ARGS_P
#undef PRE_ARGS_P
