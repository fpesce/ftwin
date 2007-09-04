#include <unistd.h>
#include <stdlib.h>
#include <check.h>

#include <apr_strings.h>

#include "debug.h"
#include "napr_heap.h"

extern apr_pool_t *main_pool;
apr_pool_t *pool;

static void setup(void)
{
    apr_status_t rs;

    rs = apr_pool_create(&pool, main_pool);
    if (rs != APR_SUCCESS) {
	DEBUG_ERR("Error creating pool");
	exit(1);
    }
}

static void teardown(void)
{
    apr_pool_destroy(pool);
}

struct check_heap_numbers_t
{
    apr_off_t size;
};
typedef struct check_heap_numbers_t check_heap_numbers_t;

static int check_heap_numbers_cmp(const void *param1, const void *param2)
{
    const check_heap_numbers_t *number1 = param1;
    const check_heap_numbers_t *number2 = param2;

    if (number1->size < number2->size) {
	return -1;
    }
    else if (number2->size < number1->size) {
	return 1;
    }

    return 0;
}

static void check_heap_numbers_display(const void *param)
{
    const check_heap_numbers_t *number = param;

    printf("%" APR_OFF_T_FMT, number->size);
}

START_TEST(test_napr_heap_unordered_bug)
{
    apr_off_t array[] = { 6298, 43601, 193288, 30460, 193288 };
    napr_heap_t *heap;
    check_heap_numbers_t *number;
    int i;

    heap = napr_heap_make_r(pool, check_heap_numbers_cmp);
    napr_heap_set_display_cb(heap, check_heap_numbers_display);

    for (i = 0; i < sizeof(array) / sizeof(apr_off_t); i++) {
	number = apr_palloc(pool, sizeof(struct check_heap_numbers_t));
	number->size = array[i];
	napr_heap_insert_r(heap, number);
    }

    for (i = 0; i < sizeof(array) / sizeof(apr_off_t); i++) {
	number = napr_heap_extract(heap);
	switch (i) {
	case 0:
	    fail_unless(number->size == 193288, "first bad ordered");
	    break;
	case 1:
	    fail_unless(number->size == 193288, "second bad ordered");
	    break;
	case 2:
	    fail_unless(number->size == 43601, "third bad ordered");
	    break;
	case 3:
	    fail_unless(number->size == 30460, "fourth bad ordered");
	    break;
	case 4:
	    fail_unless(number->size == 6298, "fifth bad ordered");
	    break;
	}
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_napr_heap_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("Napr_Heap");
    tc_core = tcase_create("Core Tests");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_napr_heap_unordered_bug);
    suite_add_tcase(s, tc_core);

    return s;
}
