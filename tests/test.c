#include <stdio.h>
#include <stdlib.h>

void
warning(void)
{
	fprintf(stderr, "It called me.\n");
}

int main(void)
{
	atexit(warning);
}

/* XXX: add mkdir test */
