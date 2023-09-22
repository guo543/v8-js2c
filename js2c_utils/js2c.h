#ifndef JS2C_H_
#define JS2C_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define JS_NULL 1
#define JS_UNDEFINED 2
#define JS_BOOLEAN 3
#define JS_NUMBER 4
#define JS_BIGINT 5
#define JS_STRING 6
#define JS_SYMBOL 7
#define JS_OBJECT 8

typedef unsigned char js_byte;
typedef unsigned int js_type;

struct data {
  js_type type;
  js_byte* value;
};

typedef struct data js_data;

js_data make_data(js_type type);
void print_typed(js_data data);

#endif
