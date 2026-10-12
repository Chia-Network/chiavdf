#include "form_common.h"
#include "libqform/mpz_qform.h"
#include  <time.h>

void mpz_qform_c(void *group, mpz_t c, mpz_t a, mpz_t b);

int main(int argc, char **argv)
{
	int i, iters = atoi(argv[1]), runtime, d_size, n_reductions = 0;
	int square = atoi(argv[2]);
	int reduce = atoi(argv[3]);
	mpz_t D;
	mpz_qform_group_t group;
	mpz_qform_t form;
	uint64_t start;

	mpz_init_set_str(D, MAIN_DISCR, 10);

	mpz_qform_group_init(&group);
	mpz_qform_group_set_discriminant(&group, D);
	mpz_qform_init(&group, &form);
	/*mpz_qform_set_id(&group, &form);*/
	mpz_set_ui(form.a, 2);
	mpz_set_ui(form.b, 1);
	mpz_qform_c(&group, form.c, form.a, form.b);
	d_size = __GMP_ABS(D->_mp_size);
	//d_size = 8;

	clock_t clock_start, clock_end;
	double cube_avg, reduce_avg;

	if (square == 1 && reduce == 1) {
		start = get_time();
		for (i = 0; i < iters; i++) {
			/*clock_start = clock();
                        mpz_qform_square(&group, &form, &form);
                        clock_end = clock();
                        cube_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));

                        clock_start = clock();
                        mpz_qform_reduce(&group, &form);
                        clock_end = clock();
                        reduce_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));
                        n_reductions++;*/

			mpz_qform_square(&group, &form, &form);
                        mpz_qform_reduce(&group, &form);
			n_reductions++;
		}
	} else if (square == 1 && reduce == 0) {
		start = get_time();
                for (i = 0; i < iters; i++) {
			/*clock_start = clock();
                        mpz_qform_square(&group, &form, &form);
                        clock_end = clock();
                        cube_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));*/

			mpz_qform_square(&group, &form, &form);
                }
	} else if (square == 0 && reduce == 1) {
		start = get_time();
                for (i = 0; i < iters; i++) {
                        /*clock_start = clock();
			mpz_qform_cube(&group, &form, &form);
			clock_end = clock();
			cube_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));

			clock_start = clock();
                        mpz_qform_reduce(&group, &form);
			clock_end = clock();
			reduce_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));
                        n_reductions++;*/

                        mpz_qform_cube(&group, &form, &form);
                        mpz_qform_reduce(&group, &form);
                        n_reductions++;
		}
	} else if (square == 0 && reduce == 0) {
		start = get_time();
                for (i = 0; i < iters; i++) {
                        /*clock_start = clock();
                        mpz_qform_cube(&group, &form, &form);
                        clock_end = clock();
                        cube_avg += ((double) (clock_end - clock_start)) / ((double)(CLOCKS_PER_SEC));*/

	                mpz_qform_cube(&group, &form, &form);

		}
	}
	mpz_qform_reduce(&group, &form);
	runtime = (int)((get_time() - start) / 1000000);
	/*gmp_printf("a = %#Zx\nb = %#Zx\nc = %#Zx\n", form.a, form.b, form.c);*/
	gmp_printf("a = %#Zx\nb = %#Zx\n", form.a, form.b);
	gmp_printf("D = %#Zx\nL = %#Zx\n", D, group.L);

	runtime = runtime ? runtime : 1;
	printf("Time: %d ms; speed: %dK ips\n", runtime, iters / runtime);
	printf("n_reductions: %d\n", n_reductions);
	//printf("cube avg: %f\n", cube_avg);
	//printf("reduce avg: %f\n", reduce_avg);
	mpz_clear(D);
	return 0;
}
