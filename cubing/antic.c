#include "form_common.h"
#include "qfb.h"

static void qfb_find_c(qfb_t form, const fmpz_t D)
{
	fmpz_mul(form->c, form->b, form->b);
	fmpz_sub(form->c, form->c, D);
	fmpz_tdiv_q_2exp(form->c, form->c, 2);
	fmpz_divexact(form->c, form->c, form->a);
}

static void print_form(qfb_t form)
{
	mpz_t a, b, c;

	mpz_inits(a, b, c, NULL);
	fmpz_get_mpz(a, form->a);
	fmpz_get_mpz(b, form->b);
	fmpz_get_mpz(c, form->c);
	/*gmp_printf("a = %#Zx\nb = %#Zx\nc = %#Zx\n", a, b, c);*/
	gmp_printf("a = %#Zx\nb = %#Zx\n", a, b);

	mpz_clears(a, b, c, NULL);
}

int main(int argc, char **argv)
{
	int i, iters = atoi(argv[1]), runtime, d_size, n_reductions = 0;
	fmpz_t D, L;
	qfb_t form;
	uint64_t start;

	fmpz_init(D);
	fmpz_set_str(D, MAIN_DISCR, 10);

	qfb_init(form);
	fmpz_set_ui(form->a, 2);
	fmpz_set_ui(form->b, 1);
	qfb_find_c(form, D);
	d_size = __GMP_ABS(COEFF_TO_PTR(*D)->_mp_size);

	fmpz_init(L);
	fmpz_neg(L, D);
	fmpz_root(L, L, 4);

	start = get_time();
	for (i = 0; i < iters; i++) {
		qfb_nudupl(form, form, D, L);
		/*qfb_nucomp(form, form, form, D, L);*/
		if (COEFF_IS_MPZ(*form->a) &&
				__GMP_ABS(COEFF_TO_PTR(*form->a)->_mp_size) > d_size) {
			qfb_reduce(form, form, D);
			n_reductions++;
		}
		/*printf("iter = %d\n", i);*/
		/*print_form(form);*/
	}
	qfb_reduce(form, form, D);
	print_form(form);
	/*gmp_printf("D = %#Zx\nL = %#Zx\n", COEFF_TO_PTR(*D), COEFF_TO_PTR(*L));*/

	runtime = (int)((get_time() - start) / 1000000);

	runtime = runtime ? runtime : 1;
	printf("Time: %d ms; speed: %dK ips\n", runtime, iters / runtime);
	printf("n_reductions: %d\n", n_reductions);

	fmpz_clear(D);
	qfb_clear(form);
	fmpz_clear(L);
	return 0;
}
