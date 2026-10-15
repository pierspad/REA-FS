#include <assert.h>
#include "syna_timing.h"
int main(void)
{
    assert(syna_remaining_timeout(2000000, 0) == 1000);
    assert(syna_remaining_timeout(2000000, 1500000) == 500);
    assert(syna_remaining_timeout(2000000, 1999999) == 1);
    assert(syna_remaining_timeout(2000000, 2000000) == 0);
    assert(syna_remaining_timeout(2000000, 2000001) == 0);
    return 0;
}
