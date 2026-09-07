// A range-based `for` over the containers this compiler ships.
//
// `range-for-class.cpp` says what the feature needs; this says that
// `include/`'s containers give it. Every one of them declares
// `iterator` as `T *` and returns one from `begin()` and `end()`, which is the
// simplification that header made on purpose - so a range-based `for` over
// them is the pointer loop and nothing else.
//
// **This is the case `~/Documents/Claude/lambdaTest/lambda.cpp` was reduced
// from** - `std::sort` with a lambda comparator and then a range-based `for`
// over the result, which was the one thing in that file cxx1 could not compile.

#include <vector>
#include <string>
#include <set>
#include <map>
#include <algorithm>
#include <cstdio>

int main(void) {
    std::vector<int> v;
    v.push_back(5); v.push_back(2); v.push_back(8); v.push_back(1);
    std::sort(v.begin(), v.end());
    for (int n : v) printf("%d ", n);
    printf("\n");

    std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
    for (int n : v) printf("%d ", n);
    printf("\n");

    std::string s = "abc";
    for (char c : s) printf("%c", c);
    printf("\n");

    std::set<int> seen;
    seen.insert(3); seen.insert(1); seen.insert(2);
    for (int n : seen) printf("%d ", n);
    printf("\n");

    std::map<int, int> m;
    m[2] = 20; m[1] = 10;
    for (std::pair<int, int> kv : m) printf("%d=%d ", kv.first, kv.second);
    printf("\n");

    // A const container reaches the const `begin()`, which is a different
    // overload returning a different type.
    const std::vector<int> &fixed = v;
    for (int n : fixed) printf("%d ", n);
    printf("\n");
    return 0;
}
