
#include "signals.hpp"

#include <string>
#include <cstdarg>
#include <cassert>

static std::string
string_printf(const char *format, ...)
__attribute__((__format__(__printf__, 1, 2)));

static std::string string_printf(const char *format, ...) {
    std::string result;
    char *str = nullptr;
    va_list args;
    va_start(args, format);
    if (vasprintf(&str, format, args) >= 0)
        result = str;
    va_end(args);
    if (str)
        free(str);
    return result;
}

static uint64_t timestamp_benchmark() {
    struct timespec tp = { 0, 0 };
    if (__builtin_expect(clock_gettime(CLOCK_MONOTONIC, &tp) < 0, 0)) {
        perror("failed in clock_gettime");
        exit(-1);
    }
    uint64_t stamp = tp.tv_sec * 1000000000ULL + tp.tv_nsec;
    return stamp;
}

struct TestCounter {
    static uint64_t get();
    static void set(uint64_t);
    static void add2(void*, uint64_t);
};

namespace { // Anon

// external symbol to prevent easy inlining
void (*test_counter_add2)(void*, uint64_t) = TestCounter::add2;

static uint64_t test_counter_var = 0;

} // Anon

class BasicSignalTests {
    static std::string accu;

    struct noncopyable {
        noncopyable(const noncopyable &) = delete;
        noncopyable& operator= (const noncopyable &) = delete;
    };

    struct Foo {
        char foo_bool(float f, int i, std::string s, const noncopyable &) {
            accu += string_printf("Foo: %.2f\n", f + i + s.size());
            return true;
        }
    };

    static char float_callback(float f, int, std::string, const noncopyable &) {
        accu += string_printf("float: %.2f\n", f);
        return 0;
    }

public:
    static void run() {
        accu = "";

        signals::signal<char (float, int, std::string, const noncopyable &)> sig1;

        auto id1 = sig1.connect(float_callback);
        auto id2 = sig1.connect([](float, int i, std::string, const noncopyable &) {
            accu += string_printf("int: %d\n", i); return 0;
        });
        auto id3 = sig1.connect([](float, int, const std::string &s, const noncopyable &) {
            accu += string_printf("string: %s\n", s.c_str()); return 0;
        });

        noncopyable nc{};
        sig1.emit(.3, 4, "huhu", nc);

        bool success;

        assert(sig1.connected(id1) == true);
        success = sig1.disconnect(id1); assert(success == true);
        assert(sig1.connected(id1) == false);
        success = sig1.disconnect(id1); assert(success == false);
        assert(sig1.connected(id2) == true);
        success = sig1.disconnect(id2); assert(success == true);
        assert(sig1.connected(id2) == false);
        success = sig1.disconnect(id3); assert(success == true);
        success = sig1.disconnect(id3); assert(success == false);
        success = sig1.disconnect(id2); assert(success == false);

        Foo foo;
        sig1.connect(foo, &Foo::foo_bool);
        sig1.connect(&foo, &Foo::foo_bool);

        sig1(.5, 1, "12", nc);

        signals::signal<void (std::string, int)> sig2;
        sig2.connect([](std::string msg, int) {
            accu += string_printf("msg: %s", msg.c_str());
        });
        sig2.connect([](std::string, int d) {
            accu += string_printf(" *%d*\n", d);
        });

        sig2.emit("in sig2", 17);

        accu += "DONE";

        const char *expected =
            "float: 0.30\n"
            "int: 4\n"
            "string: huhu\n"
            "Foo: 3.50\n"
            "Foo: 3.50\n"
            "msg: in sig2 *17*\n"
            "DONE";
        assert(accu == expected);
    }
};

std::string BasicSignalTests::accu;

class TestAggregationVector {
    static int handler1() { return 1; }
    static int handler42() { return 42; }
    static int handler777() { return 777; }

public:
    static void run() {
        signals::signal<int ()> sig_vector;
        sig_vector.connect(handler777);
        sig_vector.connect(handler42);
        sig_vector.connect(handler1);
        sig_vector.connect(handler42);
        sig_vector.connect(handler777);
        std::vector<int> results = sig_vector.emit<signals::aggregation_collation<int>>();
        const std::vector<int> reference = { 777, 42, 1, 42, 777, };
        assert(results == reference);
    }
};

class TestAggregationUniquePtrVector {
    static std::unique_ptr<int> handler1() {
        return std::unique_ptr<int>(new int(1));
    }
    static std::unique_ptr<int> handler42() {
        return std::unique_ptr<int>(new int(42));
    }
    static std::unique_ptr<int> handler777() {
        return std::unique_ptr<int>(new int(777));
    }

    static bool vecEq(const std::vector<std::unique_ptr<int>> & a,
                      const std::vector<std::unique_ptr<int>> & b) {
        if (a.size() == b.size()) {
            for (size_t i=0; i < a.size(); ++i) {
                if (*a[i] != *b[i]) return false;
            }
            return true;
        }
        else return false;
    }

public:
    static void run() {
        signals::signal<std::unique_ptr<int> ()> sig_vector;
        sig_vector.connect(handler777);
        sig_vector.connect(handler42);
        sig_vector.connect(handler1);
        sig_vector.connect(handler42);
        sig_vector.connect(handler777);
        std::vector<std::unique_ptr<int>> results =
        sig_vector.emit<signals::aggregation_collation<std::unique_ptr<int>>>();
        std::vector<std::unique_ptr<int>> reference;
        reference.push_back(std::unique_ptr<int>(new int(777)));
        reference.push_back(std::unique_ptr<int>(new int(42)));
        reference.push_back(std::unique_ptr<int>(new int(1)));
        reference.push_back(std::unique_ptr<int>(new int(42)));
        reference.push_back(std::unique_ptr<int>(new int(777)));
        assert(vecEq(results, reference));
    }
};

class TestAggregationUntil0 {
    bool check1, check2;
    TestAggregationUntil0()
        :check1{}
        ,check2{}
    {}
    bool handler_true() { check1 = true; return true; }
    bool handler_false() { check2 = true; return false; }
    bool handler_abort() { abort(); }

public:
    static void run() {
        TestAggregationUntil0 self;
        signals::signal<bool()> sig_until0;
        sig_until0.connect(self, &TestAggregationUntil0::handler_true);
        sig_until0.connect(self, &TestAggregationUntil0::handler_false);
        sig_until0.connect(self, &TestAggregationUntil0::handler_abort);
        assert(!self.check1 && !self.check2);
        const bool result = sig_until0.emit<
             signals::aggregation_last<bool>
            ,signals::condition_while<bool, true>
        >();
        assert(!result && self.check1 && self.check2);
    }
};

class TestAggregationWhile0 {
    bool check1, check2;
    TestAggregationWhile0() : check1(0), check2(0) {}
    bool handler_0() { check1 = true; return false; }
    bool handler_1() { check2 = true; return true; }
    bool handler_abort() { abort(); }

public:
    static void run() {
        TestAggregationWhile0 self;
        signals::signal<bool()> sig_while0;
        sig_while0.connect(self, &TestAggregationWhile0::handler_0);
        sig_while0.connect(self, &TestAggregationWhile0::handler_1);
        sig_while0.connect(self, &TestAggregationWhile0::handler_abort);
        assert(!self.check1 && !self.check2);
        const bool result = sig_while0.emit<
             signals::aggregation_last<bool>
            ,signals::condition_while<bool, false>
        >();
        assert(result == true && self.check1 && self.check2);
    }
};

class TestConnection {
    bool check0;
    void slot0() { check0 = true; }
    void slot1() { check0 = false; }

    TestConnection()
        :check0{}
    {}

public:
    static void run() {
        TestConnection self;
        signals::signal<void()> sig;
        signals::connection con0 = sig.connect(self, &TestConnection::slot0);

        assert(self.check0 == false);
        sig.emit();
        assert(self.check0 == true);
        con0.disconnect();
        sig.emit();
        assert(self.check0 == true);
        signals::connection con1 = sig.connect(self, &TestConnection::slot1);
        sig.emit();
        assert(self.check0 == false);
        con1.disconnect();
        self.check0 = true;
        sig.emit();
        assert(self.check0 == true);
    }
};

class TestScopedConnection {
    bool check0;
    void slot0() { check0 = true; }
    void slot1() { check0 = false; }

    TestScopedConnection()
        :check0{}
    {}

public:
    static void run() {
        TestScopedConnection self;
        signals::signal<void()> sig;
        signals::connection con0 = sig.connect(self, &TestScopedConnection::slot0);

        assert(self.check0 == false);
        sig.emit();
        assert(self.check0 == true);
        con0.disconnect();
        sig.emit();
        assert(self.check0 == true);
        signals::connection con1 = sig.connect(self, &TestScopedConnection::slot1);
        sig.emit();
        assert(self.check0 == false);
        con1.disconnect();
        self.check0 = true;
        sig.emit();
        assert(self.check0 == true);
    }
};

static void bench_simple_signal() {
    signals::signal<void (void*, uint64_t)> sig_increment;
    sig_increment.connect(test_counter_add2);
    const uint64_t start_counter = TestCounter::get();
    const uint64_t benchstart = timestamp_benchmark();
    uint64_t i;
    for(i = 0; i < 9999999; i++) {
        sig_increment.emit(nullptr, 1);
    }
    const uint64_t benchdone = timestamp_benchmark();
    const uint64_t end_counter = TestCounter::get();
    assert(end_counter - start_counter == i);
    printf("OK\n  Benchmark: Simple::Signal: %fns per emission(size=%zu): ",
           size_t(benchdone - benchstart) * 1.0 / size_t(i),
           sizeof(sig_increment));
}

static void bench_callback_loop() {
    void (*counter_increment)(void*, uint64_t) = test_counter_add2;
    const uint64_t start_counter = TestCounter::get();
    const uint64_t benchstart = timestamp_benchmark();
    uint64_t i;
    for(i = 0; i < 9999999; i++) {
        counter_increment(nullptr, 1);
    }
    const uint64_t benchdone = timestamp_benchmark();
    const uint64_t end_counter = TestCounter::get();
    assert(end_counter - start_counter == i);
    printf("OK\n  Benchmark: callback loop: %fns per round: ",
           size_t(benchdone - benchstart) * 1.0 / size_t(i));
}

uint64_t TestCounter::get() {
    return test_counter_var;
}

void TestCounter::set(uint64_t v) {
    test_counter_var = v;
}

void TestCounter::add2(void*, uint64_t v) {
    test_counter_var += v;
}

class RecursiveManipulationAndExceptionTests {
    static std::string accu;

    class Observer {
        using Signal = signals::signal<void (int, bool)>;
    public:
        Observer() {}

        // Basic receiver: no manipulation of signal
        void f1(int a, bool) {
            accu += string_printf("f1(%d) ", a);
        }

        // Manipulate signal: disconnect receiver
        void f2(int a, bool) {
            accu += string_printf("f2(%d) ", a);
            if (a == 1) m_signal.disconnect(id1);
        }

        // Manipulate signal: reconnect receiver
        void f3(int a, bool) {
            accu += string_printf("f3(%d) ", a);
            if (a == 1) id1 = m_signal.connect(this, &Observer::f1);
        }

        // Manipulate signal: connect receivers
        void f4(int a, bool) {
            accu += string_printf("f4(%d) ", a);
            if (a == 2) id6 = m_signal.connect(this, &Observer::f6);
        }

        // Recursive receiver
        void f5(int a, bool thr) {
            accu += string_printf("f5(%d) ", a);
            if (a < 3) m_signal.emit(a + 1, thr);  // Recursive step
        }

        // Exception throwing receiver
        void f6(int a, bool thr) {
            accu += string_printf("f6(%d) ", a);
            if (a == 2 && thr)
                throw std::runtime_error("f6(2) throws exception");
        }

        Signal & signal() { return m_signal; }

        signals::connection_handler id1;
        signals::connection_handler id2;
        signals::connection_handler id3;
        signals::connection_handler id4;
        signals::connection_handler id5;
        signals::connection_handler id6;

    private:
        Signal m_signal;
    };

public:
    static void run() {
        accu = "";
        Observer g;
        g.id1 = g.signal().connect(g, &Observer::f1);
        g.id2 = g.signal().connect(g, &Observer::f2);
        g.id3 = g.signal().connect(g, &Observer::f3);
        g.id4 = g.signal().connect(g, &Observer::f4);
        g.id5 = g.signal().connect(g, &Observer::f5);

        try {
            g.signal().emit(0, true);  // true => throw exception
        } catch (const std::runtime_error & e) {
            assert(std::string(e.what()) == "f6(2) throws exception");
        }

        g.signal().emit(0, false);  // false => don't throw exception

        const char * expected =
            "f1(0) f2(0) f3(0) f4(0) f5(0) "
            "f1(1) f2(1) f3(1) f4(1) f5(1) "
            "f2(2) f3(2) f4(2) f5(2) "
            "f2(3) f3(3) f4(3) f5(3) f1(3) f6(3) "
            "f1(2) f6(2) "
            // exception thrown by f6(2) here
            "f2(0) f3(0) f4(0) f5(0) "
            "f2(1) f3(1) f4(1) f5(1) "
            "f2(2) f3(2) f4(2) f5(2) "
            "f2(3) f3(3) f4(3) f5(3) f6(3) f1(3) f6(3) "
            "f6(2) f1(2) f6(2) "
            "f6(1) f1(1) f6(1) "
            "f6(0) f1(0) f6(0) ";
        assert(accu == expected);
    }
};

std::string RecursiveManipulationAndExceptionTests::accu;

int main() {
    printf("Signal/Basic Tests: ");
    BasicSignalTests::run();
    printf("OK\n");

    printf("Signal/Recursive Manipulation And Exception Tests: ");
    RecursiveManipulationAndExceptionTests::run();
    printf("OK\n");

    printf("Signal/AggregationVector: ");
    TestAggregationVector::run();
    printf("OK\n");

    printf("Signal/AggregationUniquePtrVector: ");
    TestAggregationUniquePtrVector::run();
    printf("OK\n");

    printf("Signal/AggregationUntil0: ");
    TestAggregationUntil0::run();
    printf("OK\n");

    printf("Signal/AggregationWhile0: ");
    TestAggregationWhile0::run();
    printf("OK\n");

    printf("Signal/Connection: ");
    TestConnection::run();
    printf("OK\n");

    printf("Signal/ScopedConnection: ");
    TestScopedConnection::run();
    printf("OK\n");

    printf("Signal/Benchmark: Simple::Signal: ");
    bench_simple_signal();
    printf("OK\n");

    printf("Signal/Benchmark: callback loop: ");
    bench_callback_loop();
    printf("OK\n");

    return 0;
}
