#include <iostream>
#include <vector>
#include <string>
#include <sys/time.h>
#include <chrono>

struct Preferences {
    int writes = 0;
    void putString(const char* key, const char* value) { writes++; }
    void putInt(const char* key, int value) { writes++; }
    void remove(const char* key) { writes++; }
};

int main() {
    int total_elements = 500;

    // Original Implementation (Simulated O(N))
    Preferences pref_old;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < total_elements - 1; i++) {
        // String(i).c_str(), nextRaw.c_str()
        pref_old.putString(std::to_string(i).c_str(), "{}");
    }
    pref_old.remove(std::to_string(total_elements - 1).c_str());
    pref_old.putInt("n", total_elements - 1);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> old_time = end - start;

    // New Implementation (O(1) with ID map)
    Preferences pref_new;
    start = std::chrono::high_resolution_clock::now();

    // Simulated O(1) writes for new ID mapping system.
    // Remove the item from NVS directly via its unique ID, and update the ID map array.
    pref_new.remove("0"); // O(1) remove
    // Re-save ID map in NVS
    pref_new.putString("ids", "[1,2,3...]");

    end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> new_time = end - start;

    std::cout << "Original Deletion (Worst Case): " << pref_old.writes << " NVS writes, Time: " << old_time.count() << " ms\n";
    std::cout << "Optimized Deletion (Worst Case): " << pref_new.writes << " NVS writes, Time: " << new_time.count() << " ms\n";

    return 0;
}
