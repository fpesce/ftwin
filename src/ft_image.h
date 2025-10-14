#ifndef FT_IMAGE_H
#define FT_IMAGE_H

#include "config.h"

#if HAVE_PUZZLE

#include "ft_types.h"

/**
 * @brief Compares images using libpuzzle and reports similar images.
 * @param conf The configuration structure containing file information.
 * @return APR_SUCCESS on success, or an error code.
 * @ingroup ImageComparison
 */
apr_status_t ft_image_twin_report(ft_conf_t *conf);

#endif /* HAVE_PUZZLE */

#endif /* FT_IMAGE_H */
