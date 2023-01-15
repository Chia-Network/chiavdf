#ifndef VDF_BASE_H
#define VDF_BASE_H

#if VDF_MODE==0
    #define NDEBUG
#endif

#include "Reducer.h"
#include <gmpxx.h>

//#include <cstdint>
//#include "include.h"

#include <string>
#include <vector>
#include <cstdint>

#ifndef _WIN32
#define USED __attribute__((used))
#else
#define USED
#endif

//using namespace std;
using std::string, std::vector;

typedef int64_t int64;
typedef uint64_t uint64;

void VdfBaseInit(void);

typedef __mpz_struct mpz_struct;
struct integer {
public:
    mpz_struct impl[1];

    inline ~integer() {
        mpz_clear(impl);
    }

    inline integer() {
        //assert(allow_integer_constructor);
        mpz_init(impl);
    }

    inline integer(mpz_t t) {
        mpz_init_set(impl, t);
    }

    inline integer(const integer& t) {
        mpz_init(impl);
        mpz_set(impl, t.impl);
    }

    inline integer(integer&& t) {
        mpz_init(impl);
        mpz_swap(impl, t.impl);
    }

    explicit inline integer(int i) {
        mpz_init_set_si(impl, i);
    }

    explicit inline integer(uint32_t i) {
        mpz_init_set_ui(impl, i);
    }

    explicit inline integer(int64 i) {
        mpz_init_set_si(impl, i);
    }

    explicit inline integer(uint64_t i) {
        mpz_init_set_ui(impl, i);
    }

    explicit integer(const string& s);

    explicit integer(const std::vector<uint8_t> v);

    //lsb first
    explicit integer(const vector<uint64>& data);

    integer(const uint8_t *bytes, size_t size);

    //lsb first
    vector<uint64> to_vector() const;

    vector<uint8_t> to_bytes() const;

    inline integer& operator=(const integer& t) {
        mpz_set(impl, t.impl);
        return *this;
    }

    inline integer& operator=(integer&& t) {
        mpz_swap(impl, t.impl);
        return *this;
    }

    integer& operator=(int64 i);

    integer& operator=(const string& s);

    void set_bit(int index, bool value);

    bool get_bit(int index);

    USED string to_string() const;

    string to_string_dec() const;

    integer& operator+=(const integer& t);

    integer operator+(const integer& t) const;

    integer& operator-=(const integer& t);

    integer operator-(const integer& t) const;

    integer& operator*=(const integer& t);

    integer operator*(const integer& t) const;

    integer& operator<<=(int i);

    integer operator<<(int i) const;

    integer operator-() const;

    integer& operator/=(const integer& t);

    integer operator/(const integer& t) const;

    integer& operator>>=(int i);

    integer operator>>(int i) const;

    //this is different from mpz_fdiv_r because it ignores the sign of t
    integer& operator%=(const integer& t);

    integer operator%(const integer& t) const;

    integer fdiv_r(const integer& t) const;

    bool prime() const;

    bool operator<(const integer& t) const;

    bool operator<=(const integer& t) const;

    bool operator==(const integer& t) const;

    bool operator>=(const integer& t) const;

    bool operator>(const integer& t) const;

    bool operator!=(const integer& t) const;

    bool operator<(int i) const;

    bool operator<=(int i) const;

    bool operator==(int i) const;

    bool operator>=(int i) const;

    bool operator>(int i) const;

    bool operator!=(int i) const;

    int num_bits() const;
};

integer root(const integer& t, int n);
integer CreateDiscriminant(std::vector<uint8_t>& seed, int length = 1024);

struct form {
    integer a;
    integer b;
    integer c;

    static form from_abd(const integer& t_a, const integer& t_b, const integer& d);
    static form identity(const integer& d);
    static form generator(const integer& d) {
        return from_abd(integer(2), integer(1), d);
    }
    void reduce();

    bool is_reduced();

    form inverse() const;

    bool check_valid(const integer& d);

    void assert_valid(const integer& d);

    bool operator==(const form& f) const;

    bool operator<(const form& f) const;

    //assumes this is normalized (c has the highest magnitude)
    //the inverse has the same hash
    int hash() const;
};


class PulmarkReducer {
    ClassGroupContext *t;
    Reducer *reducer;

  public:
    PulmarkReducer() {
        t=new ClassGroupContext(4096);
        reducer=new Reducer(*t);
    }

    ~PulmarkReducer() {
        delete(reducer);
        delete(t);
    }

    void reduce(form &f) {
        mpz_set(t->a, f.a.impl);
        mpz_set(t->b, f.b.impl);
        mpz_set(t->c, f.c.impl);

        reducer->run();

        mpz_set(f.a.impl, t->a);
        mpz_set(f.b.impl, t->b);
        mpz_set(f.c.impl, t->c);
    }
};

void nudupl_form(form &a, form &b, integer &D, integer &L);

form GenerateWesolowski(form &y, form &x_init,
                        integer &D, PulmarkReducer& reducer,
                        std::vector<form>& intermediates,
                        uint64_t num_iterations,
                        uint64_t k, uint64_t l);
#endif // VDF_BASE_H
