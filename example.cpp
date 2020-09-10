
#include "signals.hpp"

#include <iostream>
#include <cassert>

void func0() {
	std::cout << "func0()" << std::endl;
}


int func1_called = 0;
void func1() {
	std::cout << "func1()" << std::endl;

	++func1_called;
}

int func2_called = 0;
void func2() {
	std::cout << "func2()" << std::endl;

	++func2_called;
}

int func3_called = 0;
int func3() {
	std::cout << "func3()" << std::endl;

	return func3_called++;
}

int func4_called = 0;
int func4() {
	std::cout << "func4()" << std::endl;

	return func4_called++;
}

int main() {
	// base
	{
		signals::signal<void()> sig;
		sig.connect(func0);
		sig(); // emit
	}
	// check connection
	{
		signals::signal<void()> sig;
		auto id = sig.connect(func0);
		
		assert(sig.connected(id) == true);
		
		sig(); // emit

		sig.disconnect(id);

		assert(sig.connected(id) == false);
	}
	// scopped connection
	{
		assert(func1_called == 0);

		signals::signal<void()> sig;
		{
			signals::scoped_connection handler = sig.connect(func0);
			assert(func1_called == 0);
		}
		sig();
		
		assert(func1_called == 0);
	}
	// scopped connection
	{
		assert(func1_called == 0);

		signals::signal<void()> sig;
		{
			signals::scoped_connection handler = sig.connect(func1);
			sig();
			assert(func1_called == 1);
		}
		sig();
		
		assert(func1_called == 1);
	}
	// many slots
	{
		signals::signal<void()> sig;
		sig.connect(func2);
		sig.connect(func2);

		assert(func2_called == 0);

		sig();

		assert(func2_called == 2);
	}
	// return value
	{
		signals::signal<int()> sig;
		sig.connect(func3);

		assert(func3_called == 0);

		auto res = sig();
		assert(res == 0);
		assert(func3_called == 1);

		res = sig();
		assert(res == 1);
		assert(func3_called == 2);
	}
	// return value aggregation - only last
	{
		// by default only last value will be stored
		signals::signal<int()> sig;
		sig.connect(func4);
		sig.connect(func4);

		assert(func4_called == 0);

		auto res = sig();
		assert(res == 1);
		assert(func4_called == 2);
	}
	// return value aggregation - collation
	{
		func4_called = 0;

		signals::signal<int()> sig;
		sig.connect(func4);
		sig.connect(func4);

		assert(func4_called == 0);

		auto res = sig.emit<signals::aggregation_collation<int>>();
		assert(res.size() == 2);
		assert(res[0] == 0);
		assert(res[1] == 1);
		assert(func4_called == 2);
	}
}
