#include "js2c.h"

js_data make_data(js_type type) {
  js_data data;
  switch (type) {
    case JS_NUMBER:
      data.type = JS_NUMBER;
      data.value = (js_byte*)malloc(4);
      break;
  }
  return data;
}

js_data make_number() {
  return make_data(JS_NUMBER);
}

void print_typed(js_data data) {
  switch (data.type) {
    case JS_NUMBER:
      printf("NUMBER: %d\n", *((int32_t*)data.value));
      break;
  }
}

int main() {
  js_data num;
  num = make_number();

  num.value[0] = 0x05;
  num.value[1] = 0x00;
  num.value[2] = 0x00;
  num.value[3] = 0x00;
  print_typed(num);
}
