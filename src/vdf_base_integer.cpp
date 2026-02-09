#include "vdf_base.hpp"

#include <cassert>

#include "picosha2.h"
#include "proof_common.h"
#include "primetest.h"

#include <tuple>

static void normalize_form(integer& a, integer& b, integer& c) {
    integer r = (a - b) / (a << 1);
    integer A = a;
    integer B = b + ((r * a) << 1);
    integer C = a * r * r + b * r + c;
    a = A;
    b = B;
    c = C;
}

static void reduce_form_impl(integer& a, integer& b, integer& c) {
    integer s = (c + b) / (c << 1);
    integer A = c;
    integer B = ((s * c) << 1) - b;
    integer C = c * s * s - b * s + a;
    a = A;
    b = B;
    c = C;
}

void reduce_form(integer& a, integer& b, integer& c) {
    normalize_form(a, b, c);
    while (a > c || (a == c && b < 0)) {
        reduce_form_impl(a, b, c);
    }
    normalize_form(a, b, c);
}

integer::integer(const string& s) {
    mpz_init(impl);
    int res = mpz_set_str(impl, s.c_str(), 0);
    assert(res == 0);
    (void)res;
}

integer::integer(const std::vector<uint8_t> v) {
    mpz_init(impl);
    mpz_import(impl, v.size(), 1, sizeof(v[0]), 1, 0, &v[0]);
}

integer::integer(const vector<uint64>& data) {
    mpz_init(impl);
    mpz_import(impl, data.size(), -1, 8, 0, 0, &data[0]);
}

integer::integer(const uint8_t *bytes, size_t size) {
    mpz_init(impl);
    mpz_import(impl, size, 1, 1, 1, 0, bytes);
}

vector<uint64> integer::to_vector() const {
    vector<uint64> res;
    res.resize(mpz_sizeinbase(impl, 2) / 64 + 1, 0);

    size_t count = 0;
    mpz_export(res.data(), &count, -1, 8, 0, 0, impl);
    res.resize(count);

    return res;
}

vector<uint8_t> integer::to_bytes() const {
    vector<uint8_t> res((mpz_sizeinbase(impl, 2) + 7) / 8);
    mpz_export(res.data(), nullptr, 1, 1, 0, 0, impl);
    return res;
}

integer& integer::operator=(int64 i) {
    mpz_set_si(impl, i);
    return *this;
}

integer& integer::operator=(const string& s) {
    int res = mpz_set_str(impl, s.c_str(), 0);
    assert(res == 0);
    (void)res;
    return *this;
}

void integer::set_bit(int index, bool value) {
    if (value) {
        mpz_setbit(impl, index);
    } else {
        mpz_clrbit(impl, index);
    }
}

bool integer::get_bit(int index) {
    return mpz_tstbit(impl, index);
}

string integer::to_string() const {
    string res_string = "0x";
    res_string.resize(res_string.size() + mpz_sizeinbase(impl, 16) + 2);

    mpz_get_str(&(res_string[2]), 16, impl);

    if (res_string.substr(0, 3) == "0x-") {
        res_string.at(0) = '-';
        res_string.at(1) = '0';
        res_string.at(2) = 'x';
    }

    return res_string.c_str();
}

string integer::to_string_dec() const {
    string res_string;
    // mpz_sizeinbase() excludes sign; reserve extra for '-' and trailing '\0'.
    res_string.resize(mpz_sizeinbase(impl, 10) + 2);
    mpz_get_str(&(res_string[0]), 10, impl);
    return res_string.c_str();
}

integer& integer::operator+=(const integer& t) {
    mpz_add(impl, impl, t.impl);
    return *this;
}

integer integer::operator+(const integer& t) const {
    integer res;
    mpz_add(res.impl, impl, t.impl);
    return res;
}

integer& integer::operator-=(const integer& t) {
    mpz_sub(impl, impl, t.impl);
    return *this;
}

integer integer::operator-(const integer& t) const {
    integer res;
    mpz_sub(res.impl, impl, t.impl);
    return res;
}

integer& integer::operator*=(const integer& t) {
    mpz_mul(impl, impl, t.impl);
    return *this;
}

integer integer::operator*(const integer& t) const {
    integer res;
    mpz_mul(res.impl, impl, t.impl);
    return res;
}

integer& integer::operator<<=(int i) {
    assert(i >= 0);
    mpz_mul_2exp(impl, impl, i);
    return *this;
}

integer integer::operator<<(int i) const {
    assert(i >= 0);
    integer res;
    mpz_mul_2exp(res.impl, impl, i);
    return res;
}

integer integer::operator-() const {
    integer res;
    mpz_neg(res.impl, impl);
    return res;
}

integer& integer::operator/=(const integer& t) {
    mpz_fdiv_q(impl, impl, t.impl);
    return *this;
}

integer integer::operator/(const integer& t) const {
    integer res;
    mpz_fdiv_q(res.impl, impl, t.impl);
    return res;
}

integer& integer::operator>>=(int i) {
    assert(i >= 0);
    mpz_fdiv_q_2exp(impl, impl, i);
    return *this;
}

integer integer::operator>>(int i) const {
    assert(i >= 0);
    integer res;
    mpz_fdiv_q_2exp(res.impl, impl, i);
    return res;
}

integer& integer::operator%=(const integer& t) {
    mpz_mod(impl, impl, t.impl);
    return *this;
}

integer integer::operator%(const integer& t) const {
    integer res;
    mpz_mod(res.impl, impl, t.impl);
    return res;
}

integer integer::fdiv_r(const integer& t) const {
    integer res;
    mpz_fdiv_r(res.impl, impl, t.impl);
    return res;
}

bool integer::prime() const {
    return is_prime_bpsw(impl) != 0;
}

bool integer::operator<(const integer& t) const {
    return mpz_cmp(impl, t.impl) < 0;
}

bool integer::operator<=(const integer& t) const {
    return mpz_cmp(impl, t.impl) <= 0;
}

bool integer::operator==(const integer& t) const {
    return mpz_cmp(impl, t.impl) == 0;
}

bool integer::operator>=(const integer& t) const {
    return mpz_cmp(impl, t.impl) >= 0;
}

bool integer::operator>(const integer& t) const {
    return mpz_cmp(impl, t.impl) > 0;
}

bool integer::operator!=(const integer& t) const {
    return mpz_cmp(impl, t.impl) != 0;
}

bool integer::operator<(int i) const {
    return mpz_cmp_si(impl, i) < 0;
}

bool integer::operator<=(int i) const {
    return mpz_cmp_si(impl, i) <= 0;
}

bool integer::operator==(int i) const {
    return mpz_cmp_si(impl, i) == 0;
}

bool integer::operator>=(int i) const {
    return mpz_cmp_si(impl, i) >= 0;
}

bool integer::operator>(int i) const {
    return mpz_cmp_si(impl, i) > 0;
}

bool integer::operator!=(int i) const {
    return mpz_cmp_si(impl, i) != 0;
}

int integer::num_bits() const {
    return mpz_sizeinbase(impl, 2);
}

form form::from_abd(const integer& t_a, const integer& t_b, const integer& d) {
    form res;
    res.a = t_a;
    res.b = t_b;
    res.c = (t_b * t_b - d);

    if (t_a <= integer(0)) {
        throw std::runtime_error("Invalid form. Positive a");
    }
    if (res.c % (t_a << 2) != integer(0)) {
        throw std::runtime_error("Invalid form. Can't find c.");
    }

    res.c /= (t_a << 2);
    res.reduce();
    return res;
}

form form::identity(const integer& d) {
    return from_abd(integer(1), integer(1), d);
}

void form::reduce() {
    reduce_form(a, b, c);
}

bool form::is_reduced() {
    int a_cmp_c = mpz_cmp(a.impl, c.impl);
    if (a_cmp_c < 0 || (a_cmp_c == 0 && mpz_sgn(b.impl) >= 0)) {
        if (mpz_cmpabs(a.impl, b.impl) > 0 || mpz_cmp(a.impl, b.impl) == 0) {
            return true;
        }
    }
    return false;
}

form form::inverse() const {
    form res = *this;
    res.b = -res.b;
    res.reduce();
    return res;
}

void form::assert_valid(const integer& d) {
    assert(check_valid(d));
}

bool form::operator==(const form& f) const {
    return a == f.a && b == f.b && c == f.c;
}

bool form::operator<(const form& f) const {
    return std::make_tuple(a, b, c) < std::make_tuple(f.a, f.b, f.c);
}

int form::hash() const {
    uint64 res = c.to_vector()[0];
    return int((res >> 4) & ((1ull << 31) - 1));
}

uint64_t Prover::GetBlock(uint64_t i, uint64_t k, uint64_t T, integer& B)
{
    integer res = FastPow(2, T - k * (i + 1), B);
    mpz_mul_2exp(res.impl, res.impl, k);
    res = res / B;
    auto res_vector = res.to_vector();
    return res_vector.empty() ? 0 : res_vector[0];
}
