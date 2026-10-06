#pragma once
#include <functional>

class QObject;

namespace TextSize {
inline constexpr double Minimum = 6.0;
inline constexpr double Maximum = 32.0;
bool valid(double size);
double current();
void load();
bool set(double size, bool persist = true);
void onChange(QObject *context, std::function<void()> callback);
}
