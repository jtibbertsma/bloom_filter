#include "ruby.h"
#include "string_hash.h"

struct filter {
  size_t arycapa;
  VALUE block;
  size_t *bitary;
};

#define GET_ARYCAPA(n) (n / sizeof(size_t))
#define BITS_PER_SIZE_T (sizeof(size_t) * 8)
#define TOTAL_BITS(f) ((f)->arycapa * BITS_PER_SIZE_T)
#define CHUNK(f, bit) ((f)->bitary[bit / BITS_PER_SIZE_T])
#define BIT(bit) (1 << (bit % BITS_PER_SIZE_T))

#define FILTER_SET_BIT(f, hash) do {     \
  size_t _bit = hash % TOTAL_BITS(f);    \
  CHUNK((f),_bit) |= BIT(_bit);          \
} while (0)

#ifdef __GNUC__
#define FILTER_GET_BIT(f, hash) ({       \
  size_t _bit = hash % TOTAL_BITS(f);    \
  CHUNK((f),_bit) | BIT(_bit);           \
})
#else   /* __GNUC__ */
#define FILTER_GET_BIT(f, hash) filter_get_bit(f, hash)
static size_t
filter_get_bit(struct filter *filter, size_t hash)
{
  size_t bit = hash % TOTAL_BITS(filter);
  return CHUNK(filter, bit) | BIT(bit);
}
#endif  /* __GNUC__ */

#define NULL_FILTER (struct filter *)0

#define FILTER_CHECK(f) do {                                   \
  if ((f) && (f)->bitary == 0) {                               \
    rb_raise(rb_eRuntimeError, "Uninitialized bloom filter");  \
  }                                                            \
} while (0)

#define FILTER_GET_STRING(f, str, cstr) do {    \
  FILTER_CHECK(f);                              \
  StringValue(str);                             \
  cstr = RSTRING_PTR(str);                      \
} while (0)

static ID id_size;
static ID id_each;
static ID id_call;

static VALUE
add_item(struct filter *filter, VALUE str)
{
  char *cstr;
  size_t hash;

  FILTER_GET_STRING(filter, str, cstr);
  HASH_ITERATE(cstr, hash, {
    FILTER_SET_BIT(filter, hash);
  });

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

  if (filter->bitary) xfree(filter->bitary);
  xfree(filter);
}

static size_t
filter_memsize(const void *ptr)
{
  const struct filter *filter = ptr;
  size_t size = sizeof(struct filter);

  if (filter->bitary) {
    size += sizeof(size_t) * filter->arycapa;
  }

  return size;
}

static const rb_data_type_t filter_type = {
  "bloom_filter",
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

static VALUE
init_i(RB_BLOCK_CALL_FUNC_ARGLIST(item, ptr))
{
  struct filter *filter = (struct filter *)ptr;
  return add_item(filter, item);
}

/*
 * call-seq:
 *   BloomFilter.new(capa)                     -> filter
 *   BloomFilter.new(capa)  { |string| block } -> filter
 *   BloomFilter.new(array)                    -> filter
 *   BloomFilter.new(array) { |string| block } -> filter
 *   BloomFilter.new(enum)                     -> filter
 *   BloomFilter.new(enum)  { |string| block } -> filter
 *
 * Construct a new bloom filter.
 *
 * The argument <i>capa</i> is the desired capacity. This doesn't have to be the
 * exact number of items to be stored in the set, but the more accurate this is,
 * the more optimal the resulting bloom array in terms of false positive rate
 * and memory usage.
 *
 * Instead of passing a capacity, an array can be passed in instead. In this case,
 * the array length is used as the capacity, and item in the array is added to the
 * filter.
 *
 * If the argument is not an array or a fixnum, then it must respond to
 * <code>size</code> and <code>each</code>. The size method must return a fixnum,
 * and each item yielded by <code>each</code> will be aded to the filter.
 *
 * If a block is given, it will be called by <code>filter.query</code> when a
 * positive match is detected. The block can be set after initialization with
 * <code>filter.handler=</code>.
 */
static VALUE
filter_initialize(VALUE obj, VALUE arg)
{
  size_t nitems, arycapa;
  struct filter *filter;
  VALUE *aryptr = 0, tmp;
  int i, try_each = 0;

  switch (TYPE(arg)) {
  case T_FIXNUM:
    nitems = NUM2SIZET(arg);
    break;
  case T_ARRAY:
    nitems = RARRAY_LEN(arg);
    aryptr = RARRAY_PTR(arg);
    break;
  default:
    try_each = 1;
    tmp = rb_funcall(arg, id_size, 0, NULL);
    if (!FIXNUM_P(tmp))
      rb_raise(rb_eArgError, "Argument's size method returned an invalid size");
    nitems = NUM2SIZET(tmp);
  }
  
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  /* nitems is the desired number of elements; we need to get the
   * number of size_t needed to have one byte per item in the filter.
   */
  arycapa = GET_ARYCAPA(nitems);
  filter->arycapa = arycapa;
  if (arycapa > 0) filter->bitary = (size_t *)xcalloc(arycapa, sizeof(size_t));

  /* deal with array arg and try_each cases */
  if (aryptr) {
    for (i = 0; i < nitems; ++i) {
      add_item(filter, aryptr[i]);
    }
  }
  else if (try_each) {
    rb_block_call(arg, id_each, 0, 0, init_i, (VALUE)filter);
  }

  /* store block */
  if (rb_block_given_p()) {
    RB_OBJ_WRITE(filter, &filter->block, rb_block_proc());
  }

  return obj;
}

/*
 * call-seq:
 *   filter.add(item)   -> filter
 *   filter << item     -> filter
 *
 * Add an item to the filter. Note that any object added to the filter is coerced
 * into a string.
 */
static VALUE
filter_add_item(VALUE obj, VALUE item)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  add_item(filter, item);
  return obj;
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
filter_query_item(VALUE obj, VALUE str)
{
  char *cstr;
  struct filter *filter;
  size_t hash;

  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);
  FILTER_GET_STRING(filter, str, cstr);
  HASH_ITERATE(cstr, hash, {
    if (!FILTER_GET_BIT(filter, hash)) {
      return Qfalse;
    }
  });

  if (!NIL_P(filter->block))
    rb_funcall(filter->block, id_call, 1, str);
  return Qtrue;
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
 * when a positive match is found.
 */
static VALUE
filter_set_handler(VALUE obj, VALUE handler)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);

  RB_OBJ_WRITE(filter, &filter->block, handler);
  return handler;
}

/*
 * call-seq:
 *   filter.size      -> Number
 *   filter.length    -> Number
 *
 * Get the length of the underlying bit array in bytes.
 */
static VALUE
filter_size(VALUE obj)
{
  struct filter *filter;
  TypedData_Get_Struct(obj, struct filter, &filter_type, filter);
  return SIZET2NUM(filter->arycapa * sizeof(size_t));
}

/*
 * call-seq:
 *   BloomFilter.hash_values(str)   -> Array
 *
 * For a given object, get an array containing the hash values returned by the
 * bloom filter's hash functions.
 */
static VALUE
filter_hash_values(VALUE klass, VALUE str)
{
  char *cstr;
  size_t hash;
  VALUE ary = rb_ary_new();

  FILTER_GET_STRING(NULL_FILTER, str, cstr);
  HASH_ITERATE(cstr, hash, {
    rb_ary_push(ary, SIZET2NUM(hash));
  });

  return ary;
}

/*
 * Document-class: BloomFilter
 *
 * This is a bloom filter implementation that uses string hashes. Any object can
 * be added to the bloom filter, but objects will be converted to strings before
 * being hashed.
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
  rb_define_alias(cBloomFilter, "include?", "query");
  rb_define_method(cBloomFilter, "size", filter_size, 0);
  rb_define_alias(cBloomFilter, "length", "size");
  rb_define_singleton_method(cBloomFilter, "hash_values", filter_hash_values, 1);

  id_each = rb_intern("each");
  id_size = rb_intern("size");
  id_call = rb_intern("call");
}
