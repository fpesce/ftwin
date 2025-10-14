#ifndef FT_REPORT_JSON_H
#define FT_REPORT_JSON_H

#include "config.h"

#if HAVE_JANSSON

#include "ft_types.h"

/**
 * @brief Reports duplicate files in JSON format to stdout.
 * @param conf The configuration structure containing file information.
 * @return APR_SUCCESS on success, or an error code.
 * @ingroup Reporting
 */
apr_status_t ft_report_json(ft_conf_t *conf);

#endif /* HAVE_JANSSON */

#endif /* FT_REPORT_JSON_H */
