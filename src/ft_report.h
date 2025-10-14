#ifndef FT_REPORT_H
#define FT_REPORT_H

#include "ft_types.h"

/**
 * @brief Comparison function for sorting checksums.
 * @param chksum1 First checksum to compare.
 * @param chksum2 Second checksum to compare.
 * @return Negative if chksum1 < chksum2, 0 if equal, positive if chksum1 > chksum2.
 * @ingroup Reporting
 */
int ft_chksum_cmp(const void *chksum1, const void *chksum2);

/**
 * @brief Reports duplicate files in text format to stdout.
 * @param conf The configuration structure containing file information.
 * @return APR_SUCCESS on success, or an error code.
 * @ingroup Reporting
 */
apr_status_t ft_report_duplicates(ft_conf_t *conf);

#endif /* FT_REPORT_H */
