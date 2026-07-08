#include <iostream>
#include <vector>
#include <string>
#include <sys/time.h>
#include <cstdint>

// Mock for NVS
class Preferences {
public:
    int writes = 0;
    void putString(const char* key, const char* val) { writes++; }
    void remove(const char* key) { writes++; }
    void putInt(const char* key, int val) { writes++; }
    void putBytes(const char* key, const void* val, size_t len) { writes++; }
};

int main() {
    Preferences pref;
    int n = 100;
    int index = 0; // delete first element

    // Old way
    for (int i = index; i < n - 1; i++) {
        pref.putString(std::to_string(i).c_str(), "{}");
    }
    pref.remove(std::to_string(n - 1).c_str());
    pref.putInt("n", n - 1);

    std::cout << "Old way writes for N=100: " << pref.writes << "\n";

    // New way (mapping)
    pref.writes = 0;
    pref.remove(std::to_string(0).c_str()); // remove the item
    // save updated map
    pref.putBytes("map", nullptr, (n - 1) * sizeof(uint16_t));

    std::cout << "New way writes for N=100: " << pref.writes << "\n";

    return 0;
}
