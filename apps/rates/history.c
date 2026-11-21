/* SPDX-License-Identifier: MIT */
#include "history.h"
#include "app_days.h"
#include "app_text.h"

enum { NOMINAL = 1, VALUE, UNIT };
#define HISTORY_RESPONSE_MAX (16 * 1024)

bool history_amount(const char *text, int32_t decimals, int32_t *value)
{
    if (decimals < 0 || decimals > 4 || !text || *text < '0' || *text > '9') return false;
    int32_t number = 0, fraction = 0;
    bool dotted = false;
    while (*text) {
        if (*text == '.' && !dotted && decimals) {
            dotted = true;
            ++text;
            if (*text < '0' || *text > '9') return false;
            continue;
        }
        if (*text < '0' || *text > '9') return false;
        if (!dotted || fraction++ < decimals) {
            if (number > (INT32_MAX - (*text - '0')) / 10) return false;
            number = number * 10 + (*text - '0');
        }
        ++text;
    }
    while (fraction++ < decimals) {
        if (number > INT32_MAX / 10) return false;
        number *= 10;
    }
    *value = (int32_t)number;
    return true;
}

/* Used throughout the XML grammar; keep one whitespace classifier in the module. */
__attribute__((noinline)) static bool space(char byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

__attribute__((noinline)) static char *skip_space(char *text)
{
    while (space(*text)) ++text;
    return text;
}

__attribute__((noinline)) static int32_t date_part(const char *text, int32_t length)
{
    int32_t number = 0;
    for (int32_t index = 0; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') return -1;
        number = number * 10 + text[index] - '0';
    }
    return number;
}

int32_t history_day(const char *text, bool iso)
{
    if (app_text_length(text) < 10) return 0;
    if (text[iso ? 4 : 2] != (iso ? '-' : '.') ||
        text[iso ? 7 : 5] != (iso ? '-' : '.')) return 0;
    int32_t year = date_part(text + (iso ? 0 : 6), 4);
    int32_t month = date_part(text + (iso ? 5 : 3), 2);
    int32_t day = date_part(text + (iso ? 8 : 0), 2);
    if (year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
    int32_t encoded = app_days_from_date(year, month, day), check_year, check_month, check_day;
    app_date_from_days(encoded, &check_year, &check_month, &check_day);
    return year == check_year && month == check_month && day == check_day ? encoded : 0;
}

/* Only the numeric CBR response grammar is accepted. Entity expansion,
 * external declarations and arbitrary XML content are never needed. */
static bool attributes(char *tag, bool root, char **id, char **date)
{
    *id = *date = 0;
    while (*tag && !space(*tag)) ++tag;
    while (*tag) {
        tag = skip_space(tag);
        if (!*tag) break;
        char *name = tag;
        while (*tag && *tag != '=' && !space(*tag)) ++tag;
        char delimiter = *tag;
        if (!delimiter) return false;
        *tag++ = 0;
        if (delimiter != '=') {
            tag = skip_space(tag);
            if (*tag++ != '=') return false;
        }
        tag = skip_space(tag);
        char quote = *tag++;
        if (quote != '\"' && quote != '\'') return false;
        char *value = tag;
        while (*tag && *tag != quote) ++tag;
        if (!*tag) return false;
        *tag++ = 0;
        char **destination = app_text_equal(name, root ? "ID" : "Id") ? id :
                             app_text_equal(name, root ? "DateRange2" : "Date") ? date : 0;
        if (destination) {
            if (*destination) return false;
            *destination = value;
        }
        if (*tag && !space(*tag)) return false;
    }
    return *id && *date;
}

enum { XML, ROOT, RECORD, ROOT_END, RECORD_END, FIELD, FIELD_END = FIELD + 3, UNKNOWN = FIELD_END + 3 };

/* Attribute-bearing tags allow whitespace; field and closing tags are exact. */
static int32_t tag_kind(const char *tag)
{
    static const char names[] = "?xml\0ValCurs\0Record\0/ValCurs\0/Record\0"
                                "Nominal\0Value\0VunitRate\0/Nominal\0/Value\0/VunitRate";
    const char *name = names;
    for (int32_t kind = 0; kind < UNKNOWN; ++kind) {
        const char *at = tag;
        while (*name && *name == *at) { ++name; ++at; }
        if (!*name && (!*at || (kind < ROOT_END && space(*at)))) return kind;
        while (*name) ++name;
        ++name;
    }
    return UNKNOWN;
}

bool history_valid_id(const char *id)
{
    if (id[0] != 'R') return false;
    int32_t index = 1;
    for (; id[index]; ++index) {
        if (index >= HISTORY_ID_BYTES - 1) return false;
        if ((id[index] < '0' || id[index] > '9') && (id[index] < 'A' || id[index] > 'Z')) return false;
    }
    return index >= 2;
}

static bool finish_tag(history_parser_t *parser)
{
    char *tag = parser->tag;
    int32_t kind = tag_kind(tag);
    if (kind == XML) {
        if (parser->started || parser->declaration || !parser->tag_length || tag[parser->tag_length - 1] != '?') return false;
        parser->declaration = true;
        return true;
    }
    if (kind == ROOT) {
        char *id, *date;
        if (parser->started || parser->depth || !attributes(tag, true, &id, &date) || !history_valid_id(id)) return false;
        app_text_copy(parser->result.id, sizeof parser->result.id, id);
        if (!(parser->result.as_of = history_day(date, false))) return false;
        parser->started = true;
        parser->depth = 1;
    } else if (kind == ROOT_END) {
        if (parser->depth != 1) return false;
        parser->depth = 0;
        parser->finished = true;
    } else if (kind == RECORD) {
        char *id, *date;
        if (parser->depth != 1 || !attributes(tag, false, &id, &date) || !app_text_equal(id, parser->result.id)) return false;
        parser->day = history_day(date, false);
        if (!parser->day || parser->day > parser->result.as_of || parser->day < parser->result.as_of - HISTORY_DAYS + 1 || (parser->result.count && parser->day <= parser->result.days[parser->result.count - 1])) return false;
        parser->fields = 0;
        parser->depth = 2;
    } else if (kind == RECORD_END) {
        if (parser->depth != 2 || (parser->fields & 3) != 3 || parser->result.count == HISTORY_POINTS ||
            parser->nominal < 1 || parser->value <= 0) return false;
        int32_t normalized = parser->value / parser->nominal;
        if (normalized <= 0 || normalized > INT32_MAX) return false;
        int32_t index = parser->result.count++;
        parser->result.days[index] = parser->day;
        parser->result.values[index] = (int32_t)normalized;
        parser->depth = 1;
    } else {
        if (kind >= FIELD && kind < FIELD_END) {
            int32_t element = kind - FIELD + NOMINAL;
            if (parser->depth != 2) return false;
            parser->element = (uint8_t)element;
            uint8_t mask = (uint8_t)(1u << (element - 1));
            if (parser->fields & mask) return false;
            parser->fields |= mask;
            parser->text_length = 0;
            parser->depth = 3;
            return true;
        }
        /* Depth 3 is entered only after selecting one of the three fields. */
        int32_t closing = FIELD_END + parser->element - NOMINAL;
        if (parser->depth != 3 || kind != closing) return false;
        parser->text[parser->text_length] = 0;
        int32_t number;
        if (!history_amount(parser->text, parser->element == NOMINAL ? 0 : 4, &number) || (number <= 0 && parser->element != UNIT)) return false;
        if (parser->element == NOMINAL) {
            if (number > INT32_MAX) return false;
            parser->nominal = (int32_t)number;
        } else if (parser->element == VALUE) parser->value = number;
        parser->depth = 2;
    }
    return true;
}

void history_start(history_parser_t *parser)
{
    *parser = (history_parser_t){0};
}

bool history_feed(history_parser_t *parser, const uint8_t *data, size_t length)
{
    if (parser->broken || length > HISTORY_RESPONSE_MAX - parser->total) { parser->broken = true; return false; }
    parser->total += length;
    for (size_t index = 0; index < length; ++index) {
        char byte = (char)data[index];
        if (parser->in_tag) {
            if ((byte == '"' || byte == '\'') && (!parser->quote || parser->quote == byte)) parser->quote = parser->quote ? 0 : byte;
            if (byte == '>' && !parser->quote) {
                parser->tag[parser->tag_length] = 0;
                if (!finish_tag(parser)) { parser->broken = true; return false; }
                parser->in_tag = false;
            } else {
                if (!byte || parser->tag_length == sizeof parser->tag - 1) { parser->broken = true; return false; }
                parser->tag[parser->tag_length++] = byte;
            }
        } else if (byte == '<') {
            if (parser->finished) { parser->broken = true; return false; }
            parser->in_tag = true;
            parser->tag_length = 0;
        } else if (parser->depth == 3) {
            if (!space(byte)) {
                if (((byte != ',' || parser->element == NOMINAL) && (byte < '0' || byte > '9')) || parser->text_length == sizeof parser->text - 1) {
                    parser->broken = true; return false;
                }
                parser->text[parser->text_length++] = byte == ',' ? '.' : byte;
            }
        } else if (!space(byte)) { parser->broken = true; return false; }
    }
    return true;
}

bool history_finish(history_parser_t *parser)
{
    return !parser->broken && !parser->in_tag && parser->started && parser->finished && parser->result.count > 0;
}
