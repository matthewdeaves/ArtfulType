/*
 * test_smoke.c -- Proves the host test harness itself compiles and runs.
 *
 * Stands in until the pure markdown core (mdcore) is extracted, at which
 * point test_mdcore.c joins it here. Its only job is to give `make check`
 * a green signal on day one.
 */
#include <string.h>
#include "test_util.h"

int main(void)
{
    printf("test_smoke:\n");
    CHECK(1 == 1, "the harness runs");
    CHECK_EQ(2 + 2, 4, "arithmetic");
    CHECK_STR("hi", 2, "hi", "CHECK_STR matches");
    return TEST_RESULT();
}
