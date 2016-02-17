#include "ruby.h"

void
Init_filter_impl()
{
  VALUE cBloomFilter = rb_define_class("BloomFilter", rb_cData);
}
