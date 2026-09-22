#include <pilot/api.h>
#include <pilot/ui.h>
#include <type_traits>
static_assert(std::is_same<decltype(pilot_imu_read_result{}.gyro_x), double>::value,
              "C++ users must get the same public scalar types");
static_assert(std::is_same<decltype(pilot_magnetometer_read_result{}.sequence), uint64_t>::value,
              "Keep 64-bit sequences intact");
int main() { return PILOT_OK; }
