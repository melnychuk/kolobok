#include "../experiments/qemu-sticks3/transport_scenarios.h"

int main() {
    return stick_timing::scenarios() ? 0 : 1;
}
