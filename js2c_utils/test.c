#include "test.h"
#include <stdio.h>

int _js_entry() { int _result; add(100, 4)_result = add(1, 2); return _result; }
int main() { printf("%d\n", _js_entry()); return 0; }
