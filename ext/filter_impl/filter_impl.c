#include "ruby.h"
#include "string_hash.h"

#ifndef RB_OBJ_WRITE
#  define FILTER_SET_BLOCK(f,b) (f)->block = (b)
#else
#  define FILTER_SET_BLOCK(f,b) RUBY_OBJ_WRITE((f), &(f)->block, (b))
#endif

#define FILTER_CHECK(f) do {                                                                \
  if ((f)->bitary == 0) {                                                                   \
    rb_raise(rb_eRuntimeError, "Uninitialized bloom filter (capacity %ld)", (f)->arycapa);  \
  }                                                                                         \
} while(0)

struct filter {
  long arycapa;
  VALUE block;
  long *bitary;
};

static VALUE
add_item(struct filter *filter, VALUE item)
{
  VALUE str;
  const char *cstr;

  FILTER_GET_STRING(filter, item, str, cstr);
  HASH_ITERATE

  return str;
}

static void
filter_mark(void *ptr)
{
  struct filter *filter = ptr;
  rb_gc_mark(filter->block);
}

static void
filter_free(void *ptr)
{
  struct filter *filter = ptr;

  if (filter->bitary) ruby_xfree(filter->bitary);
  ruby_xfree(filter);
}

static size_t
filter_memsize(const void *ptr)
{
  struct filter *filter = ptr;
  size_t size = sizeof(struct filter);

  if (filter->bitary) {
    size += sizeof(long) * filter->arycapa;
  }

  return size;
}

static const rb_data_type_t filter_type = {
  {
    filter_mark,
    filter_free,
    filter_memsize
  },
  0, 0, RUBY_TYPED_FREE_IMMEDIATELY | RUBY_TYPED_WB_PROTECTED
};

static VALUE
filter_allocate(VALUE klass)
{
  struct filter *filter;
  VALUE obj = TypedData_Make_Struct(klass, struct filter, &filter_type, filter);

  filter->arycapa = 0;
  filter->block   = Qnil;
  filter->bitary  = 0;

  return obj;
}

/*
 * call-seq:
 *   BloomFilter.new(capa)                     -> filter
 *   BloomFilter.new(capa)  { |string| block } -> filter
 *   BloomFilter.new(array)                    -> filter
 *   BloomFilter.new(array) { |string| block } -> filter
 *
 * Construct a new bloom filter.
 *
 * The argument <i>capa</i> is the desired capacity. This doesn't have to be the
 * exact number of items to be stored in the set, but the more accurate this is,
 * the more optimal the resulting bloom array in terms of false positive rate
 * and memory usage.
 *
 * Instead of passing in a capacity, an array can be passed in instead. In this case,
 * the array length is used as the capacity, and item in the array is added to the
 * filter. If the argument is not an array or a fixnum, then it should respond to
 * <code>size</code> and <code>each</code>.
 *
 * If a block is given, it will be called by <code>filter.query</code> when a
 * positive match is detected. The block can be set after initialization with
 * <code>filter.handler=</code>.
 */
static VALUE
filter_initialize(VALUE obj, VALUE arg)
{
  long nitems, arycapa;
  struct filter *filter;
  VALUE *aryptr = 0, tmp;
  int i, try_each = 0;

  switch (TYPE(arg)) {
  case T_FIXNUM:
    nitems = FIX2LONG(arg);
    break;
  case T_ARRAY:
    nitems = RARRAY_LEN(arg);
    aryptr = RARRAY_CONST_PTR(arg);
    break;
  default:
    try_each = 1;
    tmp = rb_funcall(arg, rb_intern("size"), 0, NULL);
    if (!FIXNUM_P(tmp)) rb_raise(rb_eArgError, "Invalid size");
    nitems = FIX2LONG(tmp);
  }
  
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  /* nitems is the desired number of elements; we need to get the
   * number of longs needed to have one byte per item in the filter.
   */
  arycapa = GET_ARYCAPA(nitems);
  filter->arycapa = arycapa;
  if (arycapa > 0) filter->bitary = (long *)ruby_xcalloc(arycapa, sizeof(long));

  /* deal with array arg and try_each cases */
  if (aryptr) {
    for (i = 0; i < nitems; ++i) {
      add_item(filter, aryptr[i]);
    }
  }

  /* store block */
  if (rb_block_given_p()) {
    FILTER_SET_BLOCK(filter, rb_block_proc());
  }

  return obj;
}

/*
 * call-seq:
 *   filter.add(item)   -> item.to_s
 *   filter << item     -> item.to_s
 *
 * Add an item to the filter. Note that any object added to the filter is coerced
 * into a string.
 */
static VALUE
filter_add_item(VALUE obj, VALUE item)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  return add_item(filter, item);
}

/*
 * call-seq:
 *   filter.include?(item)    -> Bool
 *   filter.query(item)       -> Bool
 *
 * Test an item to see it it's in the filter. If a positive match is reported
 * and the filter has a handler Proc, the proc will be called with the string
 * object as the argument.
 */
static VALUE
filter_query_item(VALUE obj, VALUE item)
{
  VALUE str;
  const char *cstr;
  struct filter *filter;

  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);
  FILTER_GET_STRING(filter, item, str, cstr);
  HASH_ITERATE
}

/*
 * call-seq:
 *   filter.handler       -> Proc or nil
 *
 * Get the handler Proc.
 */
static VALUE
filter_handler(VALUE obj)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  return filter->block;
}

/*
 * call-seq:
 *   filter.handler = proc or nil   -> proc or nil
 *
 * Set the handler Proc. Note that we don't do type or arity checks on the Proc,
 * so setting a bogus object as the handler will cause an error to get thrown
 * down the line.
 */
static VALUE
filter_set_handler(VALUE obj, VALUE handler)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  FILTER_SET_BLOCK(filter, handler);
  return handler;
}

/*
 * Document-class: BloomFilter
 *
 * This is a bloom filter implementation that uses string hashes. Any object can
 * be added to the bloom filter, but objects will be converted to strings using
 * the to_s method before being hashed.
 *
 * This bloom filter implementation uses a ratio of 8 bits per item stored in the
 * set, yielding a 2% false positive rate. For this ratio, the optimal number of
 * hash functions is 3. See <a href="http://corte.si/posts/code/bloom-filter-rules-of-thumb/">this page</a>.
 *
 * The desired capacity is passed to the initialization method. The filter cannot
 * be resized after initialization.
 */
void
Init_filter_impl()
{
  VALUE cBloomFilter = rb_define_class("BloomFilter", rb_cData);

  rb_define_alloc_func(cBloomFilter, filter_allocate);
  rb_define_method(cBloomFilter, "initialize", filter_initialize, 1);
  rb_define_method(cBloomFilter, "handler", filter_handler, 0);
  rb_define_method(cBloomFilter, "handler=", filter_set_handler, 1);
  rb_define_method(cBloomFilter, "add", filter_add_item, 1);
  rb_define_alias(cBloomFilter, "<<", "add");
  rb_define_method(cBloomFilter, "query", filter_query_item, 1);
  rb_define_alias(cBloomFilter, "include?", query);
}
