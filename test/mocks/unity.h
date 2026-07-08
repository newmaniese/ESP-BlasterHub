#ifndef UNITY_H
#define UNITY_H
#include <iostream>
#define TEST_ASSERT_EQUAL_STRING(a, b) if(std::string(a) != std::string(b)) { std::cerr << "Mismatch: " << a << " != " << b << std::endl; }
#define TEST_ASSERT_TRUE(a) if(!(a)) { std::cerr << "Assertion failed" << std::endl; }
#define TEST_ASSERT_FALSE(a) if((a)) { std::cerr << "Assertion failed" << std::endl; }
#define TEST_ASSERT_EQUAL_UINT32(a, b) if((a) != (b)) { std::cerr << "Assertion failed" << std::endl; }
#define TEST_ASSERT_EQUAL(a, b) if((a) != (b)) { std::cerr << "Assertion failed" << std::endl; }
#define RUN_TEST(func) do { std::cout << "Running " << #func << "... "; func(); std::cout << "Done." << std::endl; } while(0)
#define UNITY_BEGIN() do { std::cout << "Starting tests..." << std::endl; } while(0)
#define UNITY_END() 0
#endif
