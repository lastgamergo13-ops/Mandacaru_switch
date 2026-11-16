// Testa a zona morta circular dos analogicos (input/stick_deadzone.h)
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "input/stick_deadzone.h"

using skyline::input::ApplyCircularDeadzone;
static int failures{};
#define CHECK(cond, ...) do { if (!(cond)) { failures++; std::printf("FALHOU: " __VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    // 0% = nao mexe em nada
    for (int x : {-32767, -1000, 0, 5, 32767})
        for (int y : {-32767, -3, 0, 7, 32767}) {
            auto [ox, oy] = ApplyCircularDeadzone(x, y, 0);
            CHECK(ox == x && oy == y, "0%% mudou (%d,%d) -> (%d,%d)", x, y, ox, oy);
        }

    // Dentro da zona morta (10% = 3276): o analogico esta parado
    for (auto [x, y] : {std::pair{0, 0}, {417, 68}, {-299, 67}, {2000, 2000}, {3276, 0}, {0, -3276}, {-2316, -2316}}) {
        auto [ox, oy] = ApplyCircularDeadzone(x, y, 10);
        CHECK(ox == 0 && oy == 0, "dentro da zona morta (%d,%d) -> (%d,%d)", x, y, ox, oy);
    }

    // O alcance maximo continua sendo alcancado, em qualquer direcao
    for (double angle = 0; angle < 6.2832; angle += 0.1) {
        int x = static_cast<int>(std::lround(32767 * std::cos(angle))), y = static_cast<int>(std::lround(32767 * std::sin(angle)));
        auto [ox, oy] = ApplyCircularDeadzone(x, y, 12);
        double magnitude = std::hypot(ox, oy);
        CHECK(std::abs(magnitude - 32767) < 2.0, "alcance maximo angulo %.2f: %.1f", angle, magnitude);
        // a direcao e mantida
        CHECK(std::abs(std::atan2(oy, ox) - std::atan2(y, x)) < 0.002 || std::abs(std::abs(std::atan2(oy, ox) - std::atan2(y, x)) - 6.2832) < 0.002, "direcao mudou no angulo %.2f", angle);
    }

    // Sem salto na borda: logo depois da zona morta o valor e pequeno e cresce de forma monotona
    int previous = 0;
    for (int x = 0; x <= 32767; x += 37) {
        auto [ox, oy] = ApplyCircularDeadzone(x, 0, 15);
        CHECK(oy == 0, "o eixo Y saiu do zero com x=%d", x);
        CHECK(ox >= previous, "nao monotono em x=%d (%d < %d)", x, ox, previous);
        CHECK(ox <= 32767, "passou do maximo em x=%d", x);
        previous = ox;
    }
    auto [edge, _] = ApplyCircularDeadzone(4915 + 40, 0, 15); // logo depois da borda (15% = 4915)
    CHECK(edge > 0 && edge < 100, "logo depois da borda deveria ser pequeno: %d", edge);

    // Valores acima do alcance nao passam do maximo (alguns controles passam de 32767)
    auto [bx, by] = ApplyCircularDeadzone(40000, 40000, 10);
    CHECK(std::hypot(bx, by) <= 32768.0, "passou do maximo: (%d,%d)", bx, by);

    // O limite de 90% evita zerar o analogico inteiro
    auto [lx, ly] = ApplyCircularDeadzone(32767, 0, 500);
    CHECK(lx == 32767 && ly == 0, "limite de 90%%: (%d,%d)", lx, ly);

    std::printf("falhas=%d\n", failures);
    return failures != 0;
}
