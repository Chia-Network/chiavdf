#include "flint/fmpz.h"

int main()
{
	fmpz_t a, b, d, inv;
	unsigned int gcd;

	fmpz_init_set_ui(a, 67309);
	fmpz_init_set_ui(b, 65536);
	fmpz_init(d);
	fmpz_init(inv);

	fmpz_gcdinv(d, inv, a, b);
	gcd = fmpz_get_ui(d);
	printf("gcd = %d\n", gcd);
	return 0;
}
