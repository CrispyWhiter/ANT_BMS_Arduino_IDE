#pragma once

namespace DiagnosticLog {

bool begin();

void write(const char *text);

void printf(const char *format, ...);

}
