#include <iostream>
#include <vector>
#include <string>

// Simulating the NVS layer
struct PreferenceStore {
    int writes = 0;
    void putString(const char* key, const char* value) {
        writes++;
    }
    void putInt(const char* key, int value) {
        writes++;
    }
    void remove(const char* key) {
        writes++;
    }
};

int main() {
    int total_elements = 500;

    // Test Scenario: Deleting index 0 (worst case)

    // Old implementation
    PreferenceStore pref_old;
    for (int i = 0; i < total_elements - 1; i++) {
        pref_old.putString(std::to_string(i).c_str(), "{}");
    }
    pref_old.remove(std::to_string(total_elements - 1).c_str());
    pref_old.putInt("n", total_elements - 1);

    std::cout << "Old approach (shifting elements) writes: " << pref_old.writes << "\n";

    // New approach (fixed keys with a master array)
    // - we remove the item using its fixed key
    // - we update the map of index -> fixed_key
    PreferenceStore pref_new;
    pref_new.remove("0");
    pref_new.putString("map", "1,2,3...");

    std::cout << "New approach writes: " << pref_new.writes << "\n";

    // New approach but simple fixed ID counter + JSON
    PreferenceStore pref_new2;
    // We can store a single string like `[1, 5, 8, ...]` for the active IDs
    // Then when deleting at index I, we just look up ID, remove ID from NVS, and rewrite the active IDs array.
    pref_new2.remove("1");
    pref_new2.putString("idx", "[5, 8, ...]");

    std::cout << "New approach 2 writes: " << pref_new2.writes << "\n";

    return 0;
}
