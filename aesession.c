/*-
 * aewm: Copyright (c) 1998-2008 Decklin Foster. See README for license.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    /* ``I gained nothing at all from Supreme Enlightenment, and for that very
     * reason it is called Supreme Enlightenment.'' -- Buddha */
    for (;;) if (wait(NULL) == -1) sleep(1);
}
