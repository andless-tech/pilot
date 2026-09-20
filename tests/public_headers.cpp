#include <pilot/api.h>
#include <type_traits>
static_assert(std::is_same<decltype(pilot_rtc_get_imu_data_result{}.gyro_x), double>::value,
              "C++ users must get the same public scalar types");
static_assert(std::is_same<decltype(pilot_rtc_get_magnetometer_data_result{}.sequence), uint64_t>::value,
              "Keep 64-bit sequences intact");
int main() { return PILOT_OK; }
