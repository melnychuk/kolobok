#pragma once

namespace roman_counts {

// No zero numeral: hidden/empty counts have an empty label. Preserve the
// dashboard's cap at 99, expressing larger counts as XCIX+ rather than 99+.
inline void format(int value, char (&label)[10]) {
  int length = 0;
  const bool capped = value > 99;
  if (capped) value = 99;
  const int values[] = {90, 50, 40, 10, 9, 5, 4, 1};
  const char* numerals[] = {"XC", "L", "XL", "X", "IX", "V", "IV", "I"};
  for (int i = 0; i < 8; ++i) {
    while (value >= values[i]) {
      value -= values[i];
      for (const char* c = numerals[i]; *c; ++c) label[length++] = *c;
    }
  }
  if (capped) label[length++] = '+';
  label[length] = '\0';
}

}  // namespace roman_counts
