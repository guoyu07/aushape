/**
 * Execve record collector
 *
 * Copyright (C) 2016 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <aushape/conv/execve_coll.h>
#include <aushape/conv/coll.h>
#include <stdio.h>
#include <string.h>

struct aushape_conv_execve_coll {
    /** Abstract base collector */
    struct aushape_conv_coll    coll;
    /** Formatted raw log buffer */
    struct aushape_gbuf raw;
    /** Formatted argument list buffer */
    struct aushape_gbuf args;
    /** Number of arguments expected */
    size_t              arg_num;
    /** Index of the argument being read */
    size_t              arg_idx;
    /** True if argument length is specified */
    bool                got_len;
    /** Index of the argument slice being read */
    size_t              slice_idx;
    /** Total length of the argument being read */
    size_t              len_total;
    /** Length of the argument read so far */
    size_t              len_read;
};

static bool
aushape_conv_execve_coll_is_valid(const struct aushape_conv_coll *coll)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    return aushape_gbuf_is_valid(&execve_coll->raw) &&
           aushape_gbuf_is_valid(&execve_coll->args) &&
           execve_coll->arg_idx <= execve_coll->arg_num &&
           (execve_coll->got_len ||
            (execve_coll->slice_idx == 0 && execve_coll->len_total == 0)) &&
           execve_coll->len_read <= execve_coll->len_total;
}

static enum aushape_rc
aushape_conv_execve_coll_init(struct aushape_conv_coll *coll,
                              const void *args)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    (void)args;
    aushape_gbuf_init(&execve_coll->raw);
    aushape_gbuf_init(&execve_coll->args);
    return AUSHAPE_RC_OK;
}

static void
aushape_conv_execve_coll_cleanup(struct aushape_conv_coll *coll)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    aushape_gbuf_cleanup(&execve_coll->raw);
    aushape_gbuf_cleanup(&execve_coll->args);
}

static bool
aushape_conv_execve_coll_is_empty(const struct aushape_conv_coll *coll)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    return execve_coll->arg_num == 0;
}

static void
aushape_conv_execve_coll_empty(struct aushape_conv_coll *coll)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    aushape_gbuf_empty(&execve_coll->raw);
    aushape_gbuf_empty(&execve_coll->args);
    execve_coll->arg_num = 0;
    execve_coll->arg_idx = 0;
    execve_coll->got_len = false;
    execve_coll->len_total = 0;
    execve_coll->len_read = 0;
}

/**
 * Exit with the specified return code if a bool expression is false.
 * To exit stores return code in "rc" variable and goes to "cleanup" label.
 *
 * @param _RC_TOKEN     The return code name without the AUSHAPE_RC_
 *                      prefix.
 * @param _bool_expr    The bool expression to evaluate.
 */
#define GUARD_BOOL(_RC_TOKEN, _bool_expr) \
    do {                                    \
        if (!(_bool_expr)) {                \
            rc = AUSHAPE_RC_##_RC_TOKEN;    \
            goto cleanup;                   \
        }                                   \
    } while (0)

/**
 * If an expression producing return code doesn't evaluate to AUSHAPE_RC_OK,
 * then exit with the evaluated return code. Stores the evaluated return code
 * in "rc" variable, goes to "cleanup" label to exit.
 *
 * @param _rc_expr  The return code expression to evaluate.
 */
#define GUARD(_rc_expr) \
    do {                            \
        rc = (_rc_expr);            \
        if (rc != AUSHAPE_RC_OK) {  \
            goto cleanup;           \
        }                           \
    } while (0)

/**
 * Process "argc" field for the execve record being collected.
 *
 * @param coll      The execve collector to process the field for.
 * @param level     Syntactic nesting level to output at.
 * @param au        The auparse context with the field to be processed as
 *                  the current field.
 *
 * @return Return code:
 *          AUSHAPE_RC_OK                   - processed succesfully,
 *          AUSHAPE_RC_AUPARSE_FAILED       - an auparse call failed,
 *          AUSHAPE_RC_CONV_INVALID_EXECVE  - invalid execve record sequence
 *                                            encountered.
 */
static enum aushape_rc
aushape_conv_execve_coll_add_argc(struct aushape_conv_coll *coll,
                                  size_t level,
                                  auparse_state_t *au)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;
    const char *str;
    size_t num;
    int end;

    assert(aushape_conv_coll_is_valid(coll));
    assert(coll->type == &aushape_conv_execve_coll_type);
    assert(au != NULL);
    (void)level;

    GUARD_BOOL(CONV_INVALID_EXECVE, execve_coll->arg_num == 0);
    str = auparse_get_field_str(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, str != NULL);
    end = 0;
    GUARD_BOOL(CONV_INVALID_EXECVE,
               sscanf(str, "%zu%n", &num, &end) >= 1 &&
               (size_t)end == strlen(str));
    execve_coll->arg_num = num;
    rc = AUSHAPE_RC_OK;
cleanup:
    assert(aushape_conv_coll_is_valid(coll));
    return rc;
}

/**
 * Add markup for the specified argument string value.
 *
 * @param coll      The execve collector to add markup to.
 * @param level     Syntactic nesting level to output at.
 * @param str       The argument string value.
 *
 * @return Return code:
 *          AUSHAPE_RC_OK                   - added succesfully,
 *          AUSHAPE_RC_NOMEM                - memory allocation failed,
 */
static enum aushape_rc
aushape_conv_execve_coll_add_arg_str(struct aushape_conv_coll *coll,
                                     size_t level,
                                     const char *str)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;

    assert(aushape_conv_coll_is_valid(coll));
    assert(coll->type == &aushape_conv_execve_coll_type);
    assert(str != NULL);

    if (coll->format.lang == AUSHAPE_LANG_XML) {
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(&execve_coll->args,
                                                     &coll->format, level));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(&execve_coll->args,
                                               "<a i=\""));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str_xml(&execve_coll->args, str));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(&execve_coll->args, "\"/>"));
    } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
        /* If it's not the first argument in the record */
        if (execve_coll->arg_idx > 0) {
            GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->args, ','));
        }
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(&execve_coll->args,
                                                     &coll->format, level));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->args, '"'));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str_json(&execve_coll->args, str));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->args, '"'));
    }

    execve_coll->arg_idx++;

    rc = AUSHAPE_RC_OK;
cleanup:
    assert(aushape_conv_coll_is_valid(coll));
    return rc;
}

/**
 * Process "a[0-9]+" field for the execve record being collected.
 *
 * @param coll      The execve collector to process the field for.
 * @param level     Syntactic nesting level to output at.
 * @param arg_idx   The argument index from the field name.
 * @param au        The auparse context with the field to be processed as the
 *                  current field.
 *
 * @return Return code:
 *          AUSHAPE_RC_OK                   - processed succesfully,
 *          AUSHAPE_RC_NOMEM                - memory allocation failed,
 *          AUSHAPE_RC_CONV_AUPARSE_FAILED  - an auparse call failed,
 *          AUSHAPE_RC_CONV_INVALID_EXECVE  - invalid execve record sequence
 *                                            encountered.
 */
static enum aushape_rc
aushape_conv_execve_coll_add_arg(struct aushape_conv_coll *coll,
                                 size_t level,
                                 size_t arg_idx,
                                 auparse_state_t *au)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;
    const char *str;

    assert(aushape_conv_coll_is_valid(coll));
    assert(coll->type == &aushape_conv_execve_coll_type);
    assert(au != NULL);

    GUARD_BOOL(CONV_INVALID_EXECVE,
               arg_idx >= execve_coll->arg_idx &&
               arg_idx < execve_coll->arg_num);

    /* Add skipped empty arguments */
    while (execve_coll->arg_idx < arg_idx) {
        rc = aushape_conv_execve_coll_add_arg_str(coll, level, "");
        if (rc != AUSHAPE_RC_OK) {
            goto cleanup;
        }
    }

    /* Add the argument in question */
    str = auparse_interpret_field(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, str != NULL);
    rc = aushape_conv_execve_coll_add_arg_str(coll, level, str);

cleanup:
    assert(aushape_conv_coll_is_valid(coll));
    return rc;
}

/**
 * Process "a[0-9]+_len" field for the execve record being collected.
 *
 * @param coll      The execve collector to process the field for.
 * @param level     Syntactic nesting level to output at.
 * @param arg_idx   The argument index from the field name.
 * @param au        The auparse context with the field to be processed as the
 *                  current field.
 *
 * @return Return code:
 *          AUSHAPE_RC_OK                   - processed succesfully,
 *          AUSHAPE_RC_CONV_AUPARSE_FAILED  - an auparse call failed,
 *          AUSHAPE_RC_CONV_INVALID_EXECVE  - invalid execve record sequence
 *                                            encountered.
 */
static enum aushape_rc
aushape_conv_execve_coll_add_arg_len(struct aushape_conv_coll *coll,
                                     size_t level,
                                     size_t arg_idx,
                                     auparse_state_t *au)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;
    const char *str;
    size_t num;
    int end;

    assert(aushape_conv_coll_is_valid(coll));
    assert(coll->type == &aushape_conv_execve_coll_type);
    assert(au != NULL);

    GUARD_BOOL(CONV_INVALID_EXECVE,
               arg_idx >= execve_coll->arg_idx &&
               arg_idx < execve_coll->arg_num &&
               !execve_coll->got_len);

    /* Add skipped empty arguments */
    while (execve_coll->arg_idx < arg_idx) {
        rc = aushape_conv_execve_coll_add_arg_str(coll, level, "");
        if (rc != AUSHAPE_RC_OK) {
            goto cleanup;
        }
    }

    execve_coll->got_len = true;

    str = auparse_get_field_str(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, str != NULL);
    end = 0;
    GUARD_BOOL(CONV_INVALID_EXECVE,
               sscanf(str, "%zu%n", &num, &end) >= 1 &&
               (size_t)end == strlen(str));
    execve_coll->len_total = num;

    rc = AUSHAPE_RC_OK;
cleanup:
    assert(aushape_conv_coll_is_valid(coll));
    return rc;
}

/**
 * Process "a[0-9]\[[0-9]+\]" field for the execve record being collected.
 *
 * @param coll      The execve collector to process the field for.
 * @param level     Syntactic nesting level to output at.
 * @param arg_idx   The argument index from the field name.
 * @param slice_idx The slice index from the field name.
 * @param au        The auparse context with the field to be processed as the
 *                  current field.
 *
 * @return Return code:
 *          AUSHAPE_RC_OK                   - processed succesfully,
 *          AUSHAPE_RC_NOMEM                - memory allocation failed,
 *          AUSHAPE_RC_CONV_AUPARSE_FAILED  - an auparse call failed,
 *          AUSHAPE_RC_CONV_INVALID_EXECVE  - invalid execve record sequence
 *                                            encountered.
 */
static enum aushape_rc
aushape_conv_execve_coll_add_arg_slice(
                            struct aushape_conv_coll *coll,
                            size_t level,
                            size_t arg_idx, size_t slice_idx,
                            auparse_state_t *au)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;
    const char *raw_str;
    size_t raw_len;
    const char *int_str;
    size_t int_len;
    size_t len;

    assert(aushape_conv_coll_is_valid(coll));
    assert(coll->type == &aushape_conv_execve_coll_type);
    assert(au != NULL);

    GUARD_BOOL(CONV_INVALID_EXECVE,
               arg_idx == execve_coll->arg_idx &&
               arg_idx < execve_coll->arg_num &&
               execve_coll->got_len &&
               slice_idx == execve_coll->slice_idx);

    raw_str = auparse_get_field_str(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, raw_str != NULL);
    raw_len = strlen(raw_str);

    int_str = auparse_interpret_field(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, int_str != NULL);
    int_len = strlen(int_str);

    /*
     * The kernel specifies the length of transferred argument in aX_len
     * fields. The transferred argument slice can be as is, or HEX-encoded.
     * However, the userspace sometimes double-quotes non-HEX-encoded
     * argument slices, making them longer than what kernel supplied.
     *
     * As that logic is not exposed, we use the safest assumption to derive
     * the length the kernel transferred. That being that only HEX-encoded
     * argument slices can be half the length when "interpreted" (decoded).
     * This seems to be true, since there is no sense in outputting an empty
     * quoted argument slice - the only quoted slice that can be half the
     * length when "interpreted".
     */
    len = (int_len == raw_len / 2) ? raw_len : int_len;
    GUARD_BOOL(CONV_INVALID_EXECVE,
               execve_coll->len_read + len <= execve_coll->len_total);

    /* If we are starting a new argument */
    if (slice_idx == 0) {
        /* Begin argument markup */
        if (coll->format.lang == AUSHAPE_LANG_XML) {
            GUARD_BOOL(NOMEM,
                       aushape_gbuf_space_opening(&execve_coll->args,
                                                  &coll->format, level));
            GUARD_BOOL(NOMEM, aushape_gbuf_add_str(&execve_coll->args,
                                                   "<a i=\""));
        } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
            /* If it's not the first argument in the record */
            if (execve_coll->arg_idx > 0) {
                GUARD_BOOL(NOMEM,
                           aushape_gbuf_add_char(&execve_coll->args, ','));
            }
            GUARD_BOOL(NOMEM,
                       aushape_gbuf_space_opening(&execve_coll->args,
                                                  &coll->format, level));
            GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->args, '"'));
        }
    }
    /* Add the slice */
    if (coll->format.lang == AUSHAPE_LANG_XML) {
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str_xml(&execve_coll->args,
                                                   int_str));
    } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str_json(&execve_coll->args,
                                                    int_str));
    }
    execve_coll->len_read += len;
    /* If we have finished the argument */
    if (execve_coll->len_read == execve_coll->len_total) {
        /* End argument markup */
        if (coll->format.lang == AUSHAPE_LANG_XML) {
            GUARD_BOOL(NOMEM, aushape_gbuf_add_str(&execve_coll->args,
                                                   "\"/>"));
        } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
            GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->args, '"'));
        }
        execve_coll->got_len = 0;
        execve_coll->slice_idx = 0;
        execve_coll->len_total = 0;
        execve_coll->len_read = 0;
        execve_coll->arg_idx++;
    } else {
        execve_coll->slice_idx++;
    }

    rc = AUSHAPE_RC_OK;
cleanup:
    assert(aushape_conv_coll_is_valid(coll));
    return rc;
}

static enum aushape_rc
aushape_conv_execve_coll_add(struct aushape_conv_coll *coll,
                             size_t level,
                             bool *pfirst,
                             auparse_state_t *au)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc;
    size_t l = level;
    const char *raw;
    const char *field_name;
    size_t arg_idx;
    size_t slice_idx;
    int end;

    (void)pfirst;

    if (coll->format.lang == AUSHAPE_LANG_XML) {
        l++;
    } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
        l+=2;
    }

    /*
     * If it's not the first argument being processed,
     * and so it's not the first record
     */
    if (execve_coll->arg_idx > 0) {
        GUARD_BOOL(NOMEM, aushape_gbuf_add_char(&execve_coll->raw, '\n'));
    }
    raw = auparse_get_record_text(au);
    GUARD_BOOL(CONV_AUPARSE_FAILED, raw != NULL);
    GUARD_BOOL(NOMEM, aushape_gbuf_add_str(&execve_coll->raw, raw));

    /*
     * For each field in the record
     */
    GUARD_BOOL(CONV_INVALID_EXECVE, auparse_first_field(au) != 0);
    do {
        field_name = auparse_get_field_name(au);
        GUARD_BOOL(CONV_AUPARSE_FAILED, field_name != NULL);
        /* If it's the "type" pseudo-field */
        if (strcmp(field_name, "type") == 0) {
            continue;
        /* If it's the "node" which we handle at event level */
        } else if (strcmp(field_name, "node") == 0) {
            continue;
        /* If it's the number of arguments */
        } else if (strcmp(field_name, "argc") == 0) {
            GUARD(aushape_conv_execve_coll_add_argc(coll, l, au));
        /* If it's a whole argument */
        } else if (end = 0,
                   sscanf(field_name, "a%zu%n", &arg_idx, &end) >= 1 &&
                   (size_t)end == strlen(field_name)) {
            GUARD(aushape_conv_execve_coll_add_arg(coll, l, arg_idx, au));
        /* If it's the length of an argument */
        } else if (end = 0,
                   sscanf(field_name, "a%zu_len%n", &arg_idx, &end) >= 1 &&
                   (size_t)end == strlen(field_name)) {
            GUARD(aushape_conv_execve_coll_add_arg_len(coll, l, arg_idx, au));
        /* If it's an argument slice */
        } else if (end = 0,
                   sscanf(field_name, "a%zu[%zu]%n",
                          &arg_idx, &slice_idx, &end) >= 2 &&
                   (size_t)end == strlen(field_name)) {
            GUARD(aushape_conv_execve_coll_add_arg_slice(coll, l, arg_idx,
                                                         slice_idx, au));
        /* If it's something else */
        } else {
            rc = AUSHAPE_RC_CONV_INVALID_EXECVE;
            goto cleanup;
        }
    } while (auparse_next_field(au) > 0);

    rc = AUSHAPE_RC_OK;
cleanup:
    return rc;
}

static enum aushape_rc
aushape_conv_execve_coll_end(struct aushape_conv_coll *coll,
                             size_t level,
                             bool *pfirst)
{
    struct aushape_conv_execve_coll *execve_coll =
                    (struct aushape_conv_execve_coll *)coll;
    enum aushape_rc rc = AUSHAPE_RC_OK;
    struct aushape_gbuf *gbuf = coll->gbuf;
    size_t l = level;

    /* Output prologue */
    if (coll->format.lang == AUSHAPE_LANG_XML) {
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "<execve raw=\""));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_buf_xml(gbuf,
                                                   execve_coll->raw.ptr,
                                                   execve_coll->raw.len));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "\">"));
    } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
        if (!*pfirst) {
            GUARD_BOOL(NOMEM, aushape_gbuf_add_char(gbuf, ','));
        }
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "\"execve\":{"));
        l++;
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "\"raw\":\""));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_buf_json(gbuf,
                                                    execve_coll->raw.ptr,
                                                    execve_coll->raw.len));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "\","));
        GUARD_BOOL(NOMEM, aushape_gbuf_space_opening(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "\"args\":["));
    }
    l++;

    /* Add skipped empty arguments */
    while (execve_coll->arg_idx < execve_coll->arg_num) {
        rc = aushape_conv_execve_coll_add_arg_str(coll, l, "");
        if (rc != AUSHAPE_RC_OK) {
            goto cleanup;
        }
    }

    /* Output arguments */
    GUARD_BOOL(NOMEM, aushape_gbuf_add_buf(gbuf,
                                           execve_coll->args.ptr,
                                           execve_coll->args.len));

    l--;
    /* Output epilogue */
    if (coll->format.lang == AUSHAPE_LANG_XML) {
        GUARD_BOOL(NOMEM, aushape_gbuf_space_closing(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_str(gbuf, "</execve>"));
    } else if (coll->format.lang == AUSHAPE_LANG_JSON) {
        if (execve_coll->args.len > 0) {
            GUARD_BOOL(NOMEM, aushape_gbuf_space_closing(gbuf, &coll->format, l));
        }
        GUARD_BOOL(NOMEM, aushape_gbuf_add_char(gbuf, ']'));
        l--;
        GUARD_BOOL(NOMEM, aushape_gbuf_space_closing(gbuf, &coll->format, l));
        GUARD_BOOL(NOMEM, aushape_gbuf_add_char(gbuf, '}'));
    }

    assert(l == level);
    *pfirst = false;
    rc = AUSHAPE_RC_OK;
cleanup:
    return rc;
}

const struct aushape_conv_coll_type aushape_conv_execve_coll_type = {
    .size       = sizeof(struct aushape_conv_execve_coll),
    .init       = aushape_conv_execve_coll_init,
    .is_valid   = aushape_conv_execve_coll_is_valid,
    .cleanup    = aushape_conv_execve_coll_cleanup,
    .is_empty   = aushape_conv_execve_coll_is_empty,
    .empty      = aushape_conv_execve_coll_empty,
    .add        = aushape_conv_execve_coll_add,
    .end        = aushape_conv_execve_coll_end,
};
