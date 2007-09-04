#include <stdlib.h>
#include <check.h>
#include <stdio.h>

#include <apr.h>
#include <apr_pools.h>
#ifdef HAVE_CONFIG_H
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_BUGREPORT
#include "config.h"
#endif

apr_pool_t *main_pool = NULL;

Suite *make_napr_heap_suite(void);

int main(int argc, char **argv)
{
    char buf[256];
    int nf;
    apr_status_t status;
    SRunner *sr;
    int num = 0;

    if (argc > 1) {
	num = atoi(argv[1]);
    }

    status = apr_initialize();
    if (APR_SUCCESS != status) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    atexit(apr_terminate);

    if ((status = apr_pool_create(&main_pool, NULL)) != APR_SUCCESS) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    sr = srunner_create(NULL);

    if (!num || num == 1)
	srunner_add_suite(sr, make_napr_heap_suite());

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_set_xml(sr, "check_log.xml");

    srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    apr_terminate();
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
