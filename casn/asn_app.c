#include <casn/asn_app.h>

static u8 * format_asn_app_user_type_enum (u8 * s, va_list * va)
{
  asn_app_user_type_enum_t x = va_arg (*va, int);
  char * t = 0;
  switch (x)
    {
#define _(f) case ASN_APP_USER_TYPE_##f: t = #f; break;
      foreach_asn_app_user_type
#undef _
    default:
      return format (s, "unknown %d", x);
    }
  vec_add (s, t, strlen (t));
  return s;
}

static u32 asn_app_add_oneof_attribute_helper (asn_app_attribute_t * pa, u8 * choice);

static void
serialize_vec_asn_app_photo (serialize_main_t * m, va_list * va)
{
  asn_app_photo_t * p = va_arg (*va, asn_app_photo_t *);
  u32 n = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n; i++)
    {
      vec_serialize (m, p[i].thumbnail_as_image_data, serialize_vec_8);
      vec_serialize (m, p[i].blob_name_for_raw_data, serialize_vec_8);
    }
}

static void
unserialize_vec_asn_app_photo (serialize_main_t * m, va_list * va)
{
  asn_app_photo_t * p = va_arg (*va, asn_app_photo_t *);
  u32 n = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n; i++)
    {
      vec_unserialize (m, &p[i].thumbnail_as_image_data, unserialize_vec_8);
      vec_unserialize (m, &p[i].blob_name_for_raw_data, unserialize_vec_8);
    }
}

always_inline asn_app_attribute_type_t
asn_app_attribute_value_type (asn_app_attribute_t * pa)
{
  asn_app_attribute_type_t type = pa->type;
  if (type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice)
    {
      u32 n_values = hash_elts (pa->oneof_index_by_value);
      if (n_values < (1 << BITS (u8)))
	type = ASN_APP_ATTRIBUTE_TYPE_u8;
      else if (n_values < (1 << BITS (u16)))
	type = ASN_APP_ATTRIBUTE_TYPE_u16;
      else
	type = ASN_APP_ATTRIBUTE_TYPE_u32;
    }
  else if (type == ASN_APP_ATTRIBUTE_TYPE_oneof_multiple_choice)
    {
      u32 n_values = hash_elts (pa->oneof_index_by_value);
      if (n_values <= BITS (u8))
	type = ASN_APP_ATTRIBUTE_TYPE_u8;
      else if (n_values <= BITS (u16))
	type = ASN_APP_ATTRIBUTE_TYPE_u16;
      else if (n_values <= BITS (u32))
	type = ASN_APP_ATTRIBUTE_TYPE_u32;
      else if (n_values <= BITS (u64))
	type = ASN_APP_ATTRIBUTE_TYPE_u64;
      else
	type = ASN_APP_ATTRIBUTE_TYPE_bitmap;
    }
  return type;
}

always_inline void asn_app_attribute_free (asn_app_attribute_t * a)
{
  uword i;
  asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);
  switch (value_type)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
    case ASN_APP_ATTRIBUTE_TYPE_u16:
    case ASN_APP_ATTRIBUTE_TYPE_u32:
    case ASN_APP_ATTRIBUTE_TYPE_u64:
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_free (a->values.as_u8);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_foreach_index (i, a->values.as_string)
	vec_free (a->values.as_string[i]);
      vec_free (a->values.as_string);
      break;
    default:
      ASSERT (0);
      break;
    }
  clib_bitmap_free (a->value_is_valid_bitmap);
  vec_foreach_index (i, a->oneof_values) vec_free (a->oneof_values[i]);
  vec_free (a->oneof_values);
  hash_free (a->oneof_index_by_value);
  vec_free (a->oneof_map_for_unserialize);
  vec_free (a->name);
}

static void
serialize_asn_app_attribute_value (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);

  switch (value_type)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      vec_validate (a->values.as_u8, i);
      serialize_integer (m, a->values.as_u8[i], sizeof (u8));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      vec_validate (a->values.as_u16, i);
      serialize_integer (m, a->values.as_u16[i], sizeof (u16));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      vec_validate (a->values.as_u32, i);
      serialize_integer (m, a->values.as_u32[i], sizeof (u32));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      vec_validate (a->values.as_u64, i);
      serialize_integer (m, a->values.as_u64[i], sizeof (u64));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_validate (a->values.as_f64, i);
      serialize (m, serialize_f64, a->values.as_f64[i]);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_validate (a->values.as_string, i);
      vec_serialize (m, a->values.as_string[i], serialize_vec_8);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vec_validate (a->values.as_bitmap, i);
      serialize_bitmap (m, a->values.as_bitmap[i]);
      break;
    default:
      ASSERT (0);
      break;
    }
}

static void
unserialize_asn_app_attribute_value (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  asn_app_attribute_type_t vta = asn_app_attribute_value_type (a);
  asn_app_attribute_type_t vtu = vta;
  if (asn_app_attribute_is_oneof (a))
    vtu = a->oneof_value_type_for_unserialize;
  union {
    u8 as_u8;
    u16 as_u16;
    u32 as_u32;
    u64 as_u64;
    f64 as_f64;
    u8 * as_string;
    uword * as_bitmap;
  } vu;
  switch (vtu)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      unserialize_integer (m, &vu.as_u8, sizeof (u8));
      vu.as_u64 = vu.as_u8;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      unserialize_integer (m, &vu.as_u16, sizeof (u16));
      vu.as_u64 = vu.as_u16;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      unserialize_integer (m, &vu.as_u32, sizeof (u32));
      vu.as_u64 = vu.as_u32;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      unserialize_integer (m, &vu.as_u64, sizeof (u64));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      unserialize (m, unserialize_f64, &vu.as_f64);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_unserialize (m, &vu.as_string, unserialize_vec_8);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vu.as_bitmap = unserialize_bitmap (m);
      break;
    default:
      ASSERT (0);
      break;
    }

  switch (vta)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      vec_validate (a->values.as_u8, i);
      a->values.as_u8[i] = vu.as_u64;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      vec_validate (a->values.as_u16, i);
      a->values.as_u16[i] = vu.as_u64;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      vec_validate (a->values.as_u32, i);
      a->values.as_u32[i] = vu.as_u64;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      vec_validate (a->values.as_u64, i);
      a->values.as_u64[i] = vu.as_u64;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_validate (a->values.as_f64, i);
      a->values.as_f64[i] = vu.as_f64;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_validate (a->values.as_string, i);
      a->values.as_string[i] = vu.as_string;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vec_validate (a->values.as_bitmap, i);
      a->values.as_bitmap[i] = vu.as_bitmap;
      break;
    default:
      ASSERT (0);
      break;
    }
}

static void
serialize_asn_app_attribute_value_oneof_single_choice (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  u8 * v = asn_app_get_oneof_attribute (am, ai, i);
  vec_serialize (m, v, serialize_vec_8);
}

static void
unserialize_asn_app_attribute_value_oneof_single_choice (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  u8 * choice;
  vec_unserialize (m, &choice, unserialize_vec_8);
  asn_app_set_oneof_attribute (am, ai, i, "%v", choice);
  vec_free (choice);
}

static void
serialize_asn_app_attribute_value_oneof_multiple_choice (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  uword j, * b = 0;
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  b = asn_app_get_oneof_attribute_multiple_choice_bitmap (am, ai, i, b);
  serialize_likely_small_unsigned_integer (m, clib_bitmap_count_set_bits (b));
  clib_bitmap_foreach (j, b, ({
        vec_serialize (m, a->oneof_values[j], serialize_vec_8);
      }));
}

static void
unserialize_asn_app_attribute_value_oneof_multiple_choice (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 ai = va_arg (*va, u32);
  u32 i = va_arg (*va, u32);
  uword n_set = unserialize_likely_small_unsigned_integer (m);
  while (n_set != 0)
    {
      u8 * choice;
      vec_unserialize (m, &choice, unserialize_vec_8);
      asn_app_set_oneof_attribute (am, ai, i, "%v", choice);
      vec_free (choice);
      n_set--;
    }
}

static void
serialize_asn_app_attribute_main (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  serialize_likely_small_unsigned_integer (m, vec_len (am->attributes));
  asn_app_attribute_t * a;
  vec_foreach (a, am->attributes)
    {
      asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);
      vec_serialize (m, a->name, serialize_vec_8);
      if (asn_app_attribute_is_oneof (a))
        {
          uword i;
          serialize_likely_small_unsigned_integer (m, value_type);
          serialize_likely_small_unsigned_integer (m, vec_len (a->oneof_values));
          for (i = 0; i < vec_len (a->oneof_values); i++)
            vec_serialize (m, a->oneof_values[i], serialize_vec_8);
        }
      serialize_bitmap (m, a->value_is_valid_bitmap);
    }
}

static void
unserialize_asn_app_attribute_main (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  asn_app_attribute_t * a;
  u32 n_attrs = unserialize_likely_small_unsigned_integer (m);
  u32 i;
  vec_resize (am->attribute_map_for_unserialize, n_attrs);
  for (i = 0; i < n_attrs; i++)
    {
      u8 * name;
      uword * p;
      vec_unserialize (m, &name, unserialize_vec_8);
      if (! (p = hash_get_mem (am->attribute_by_name, name)))
        serialize_error_return (m, "unknown attribute named `%v'", name);
      vec_free (name);
      a = vec_elt_at_index (am->attributes, *p);
      am->attribute_map_for_unserialize[i] = a->index;
      if (asn_app_attribute_is_oneof (a))
        {
          uword n_oneof_values, i;
          a->oneof_value_type_for_unserialize = unserialize_likely_small_unsigned_integer (m);
          n_oneof_values = unserialize_likely_small_unsigned_integer (m);
          for (i = 0; i < n_oneof_values; i++)
            {
              u32 oi;
              vec_unserialize (m, &name, unserialize_vec_8);

	      /* One of value with index zero is always zero. */
	      if (i == 0 && vec_len (name) == 0)
		{
		  vec_free (name);
		  continue;
		}

	      ASSERT (vec_len (name) > 0);

              p = hash_get_mem (a->oneof_index_by_value, name);
              if (! p)
                oi = asn_app_add_oneof_attribute_helper (a, name);
              else
                {
                  oi = p[0];
                  vec_free (name);
                }
              vec_validate (a->oneof_map_for_unserialize, i);
              a->oneof_map_for_unserialize[i] = oi;
            }
        }
      a->value_is_valid_bitmap = unserialize_bitmap (m);
    }
}

static void
serialize_asn_app_attributes_for_index (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 vi = va_arg (*va, u32);
  asn_app_attribute_t * a;
  vec_foreach (a, am->attributes)
    {
      if (clib_bitmap_get (a->value_is_valid_bitmap, vi))
        serialize (m, serialize_asn_app_attribute_value, am, a - am->attributes, vi);
    }
}

static void
unserialize_asn_app_attributes_for_index (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 vi = va_arg (*va, u32);
  u32 i;
  asn_app_attribute_t * a;
  for (i = 0; i < vec_len (am->attribute_map_for_unserialize); i++)
    {
      a = vec_elt_at_index (am->attributes, am->attribute_map_for_unserialize[i]);
      if (clib_bitmap_get (a->value_is_valid_bitmap, vi))
        unserialize (m, unserialize_asn_app_attribute_value, am, a - am->attributes, vi);
    }
}

static void
serialize_asn_app_profile_attributes_for_index (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 vi = va_arg (*va, u32);
  asn_app_attribute_t * a;
  vec_foreach (a, am->attributes)
    if (clib_bitmap_get (a->value_is_valid_bitmap, vi))
      {
        serialize_function_t * f;
        ASSERT (vec_len (a->name) > 0);
        vec_serialize (m, a->name, serialize_vec_8);
        if (asn_app_attribute_is_oneof (a))
          {
            u32 is_single = a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice;
            f = (is_single
                 ? serialize_asn_app_attribute_value_oneof_single_choice
                 : serialize_asn_app_attribute_value_oneof_multiple_choice);
          }
        else
          f = serialize_asn_app_attribute_value;
        serialize (m, f, am, a - am->attributes, vi);
      }

  /* Serialize empty name to mark end. */
  vec_serialize (m, (u8 *) 0, serialize_vec_8);
}

static void
unserialize_asn_app_profile_attributes_for_index (serialize_main_t * m, va_list * va)
{
  asn_app_attribute_main_t * am = va_arg (*va, asn_app_attribute_main_t *);
  u32 vi = va_arg (*va, u32);
  asn_app_attribute_t * a;
  clib_error_t * error = 0;

  vec_foreach (a, am->attributes)
    asn_app_invalidate_attribute (am, a->index, vi);

  while (1)
    {
      u8 * name;
      uword is_last, * p;

      vec_unserialize (m, &name, unserialize_vec_8);

      is_last = vec_len (name) == 0;
      if (! is_last)
        {
          if (! (p = hash_get_mem (am->attribute_by_name, name)))
            error = clib_error_return (0, "unknown attribute named `%v'", name);
          else
            {
              serialize_function_t * f;

              a = vec_elt_at_index (am->attributes, p[0]);
              if (asn_app_attribute_is_oneof (a))
                {
                  u32 is_single = a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice;
                  f = (is_single
                       ? unserialize_asn_app_attribute_value_oneof_single_choice
                       : unserialize_asn_app_attribute_value_oneof_multiple_choice);
                }
              else
                f = unserialize_asn_app_attribute_value;
              unserialize (m, f, am, a - am->attributes, vi);
            }
        }

      vec_free (name);

      if (error)
        serialize_error (&m->header, error);

      if (is_last)
        break;
    }
}

void * asn_app_get_attribute (asn_app_attribute_main_t * am, u32 ai, u32 ui)
{
  asn_app_attribute_t * pa = vec_elt_at_index (am->attributes, ai);
  asn_app_attribute_type_t type = asn_app_attribute_value_type (pa);

  switch (type)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      vec_validate (pa->values.as_u8, ui);
      return pa->values.as_u8 + ui;

    case ASN_APP_ATTRIBUTE_TYPE_u16:
      vec_validate (pa->values.as_u16, ui);
      return pa->values.as_u16 + ui;

    case ASN_APP_ATTRIBUTE_TYPE_u32:
      vec_validate (pa->values.as_u32, ui);
      return pa->values.as_u32 + ui;

    case ASN_APP_ATTRIBUTE_TYPE_u64:
      vec_validate (pa->values.as_u64, ui);
      return pa->values.as_u64 + ui;

    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_validate (pa->values.as_f64, ui);
      return pa->values.as_f64 + ui;

    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_validate (pa->values.as_string, ui);
      return pa->values.as_string + ui;

    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vec_validate (pa->values.as_bitmap, ui);
      return pa->values.as_bitmap + ui;

    default:
      ASSERT (0);
      return 0;
    }
}

void asn_app_set_attribute (asn_app_attribute_main_t * am, u32 ai, u32 i, ...)
{
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  va_list va;
  va_start (va, i);
  asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);

  a->value_is_valid_bitmap = clib_bitmap_ori (a->value_is_valid_bitmap, i);
  switch (value_type)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      vec_validate (a->values.as_u8, i);
      a->values.as_u8[i] = va_arg (va, u32);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      vec_validate (a->values.as_u16, i);
      a->values.as_u16[i] = va_arg (va, u32);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      vec_validate (a->values.as_u32, i);
      a->values.as_u32[i] = va_arg (va, u32);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      vec_validate (a->values.as_u64, i);
      a->values.as_u64[i] = va_arg (va, u64);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vec_validate (a->values.as_bitmap, i);
      a->values.as_bitmap[i] = clib_bitmap_ori (a->values.as_bitmap[i], va_arg (va, u32));
      break;
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_validate (a->values.as_f64, i);
      a->values.as_f64[i] = va_arg (va, f64);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      {
	char * fmt = va_arg (va, char *);
	vec_validate (a->values.as_string, i);
	a->values.as_string[i] = va_format (a->values.as_string[i], fmt, &va);
	break;
      }
    default:
      ASSERT (0);
      break;
    }
}

void asn_app_invalidate_attribute (asn_app_attribute_main_t * am, u32 ai, u32 i)
{
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);

  a->value_is_valid_bitmap = clib_bitmap_andnoti (a->value_is_valid_bitmap, i);
  switch (value_type)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      vec_validate (a->values.as_u8, i);
      a->values.as_u8[i] = 0;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      vec_validate (a->values.as_u16, i);
      a->values.as_u16[i] = 0;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      vec_validate (a->values.as_u32, i);
      a->values.as_u32[i] = 0;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      vec_validate (a->values.as_u64, i);
      a->values.as_u64[i] = 0;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      vec_validate (a->values.as_bitmap, i);
      clib_bitmap_free (a->values.as_bitmap[i]);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_f64:
      vec_validate (a->values.as_f64, i);
      a->values.as_f64[i] = 0;
      break;
    case ASN_APP_ATTRIBUTE_TYPE_string:
      vec_validate (a->values.as_string, i);
      vec_free (a->values.as_string[i]);
      break;
    default:
      ASSERT (0);
      break;
    }
}

u32 asn_app_add_attribute (asn_app_attribute_main_t * am, asn_app_attribute_type_t type, char * fmt, ...)
{
  asn_app_attribute_t * pa;
  va_list va;
  va_start (va, fmt);
  vec_add2 (am->attributes, pa, 1);
  pa->type = type;
  pa->index = pa - am->attributes;
  pa->name = va_format (0, fmt, &va);
  va_end (va);

  if (! am->attribute_by_name)
    am->attribute_by_name = hash_create_vec (0, sizeof (pa->name[0]), sizeof (uword));
  
  hash_set_mem (am->attribute_by_name, pa->name, pa->index);

  return pa->index;
}

static u32 asn_app_add_oneof_attribute_helper (asn_app_attribute_t * pa, u8 * choice)
{
  ASSERT (pa->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice
	  || pa->type == ASN_APP_ATTRIBUTE_TYPE_oneof_multiple_choice);

  if (! pa->oneof_index_by_value)
    pa->oneof_index_by_value = hash_create_vec (0, sizeof (choice[0]), sizeof (uword));

  {
    uword * p = hash_get (pa->oneof_index_by_value, choice);
    if (p)
      {
        vec_free (choice);
        return p[0];
      }
  }

  uword is_single = pa->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice;
  uword vi = vec_len (pa->oneof_values);

  ASSERT (choice != 0);
  hash_set_mem (pa->oneof_index_by_value, choice, vi);
  vec_add1 (pa->oneof_values, choice);

  if ((is_single && vi == (1 << BITS (u8))) || (! is_single && vi == BITS (u8)))
    {
      u32 i;
      u16 * v16;
      vec_clone (v16, pa->values.as_u8);
      vec_foreach_index (i, pa->values.as_u8)
	v16[i] = pa->values.as_u8[i];
      vec_free (pa->values.as_u8);
      pa->values.as_u16 = v16;
    }
  else if ((is_single && vi == (1 << BITS (u16))) || (! is_single && vi == BITS (u16)))
    {
      u32 i;
      u32 * v32;
      vec_clone (v32, pa->values.as_u16);
      vec_foreach_index (i, pa->values.as_u16)
	v32[i] = pa->values.as_u16[i];
      vec_free (pa->values.as_u16);
      pa->values.as_u32 = v32;
    }
  else if (! is_single && vi == BITS (u32))
    {
      u32 i;
      u64 * v64;
      vec_clone (v64, pa->values.as_u32);
      vec_foreach_index (i, pa->values.as_u32)
	v64[i] = pa->values.as_u32[i];
      vec_free (pa->values.as_u32);
      pa->values.as_u64 = v64;
    }

  else if (! is_single && vi == BITS (u64))
    {
      u32 i;
      uword ** as_bitmap;
      vec_clone (as_bitmap, pa->values.as_u64);
      vec_foreach_index (i, pa->values.as_u64)
	vec_add1 (as_bitmap[i], pa->values.as_u64[i]);
      vec_free (pa->values.as_u64);
      pa->values.as_bitmap = as_bitmap;
    }

  return vi;
}

u32 asn_app_add_oneof_attribute (asn_app_attribute_main_t * am, u32 ai, char * fmt, ...)
{
  asn_app_attribute_t * pa = vec_elt_at_index (am->attributes, ai);

  u8 * choice = 0;
  va_list va;
  va_start (va, fmt);
  choice = va_format (choice, fmt, &va);
  va_end (va);

  return asn_app_add_oneof_attribute_helper (pa, choice);
}

void asn_app_set_oneof_attribute (asn_app_attribute_main_t * am, u32 ai, u32 i, char * fmt, ...)
{
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  uword is_single, is_clear, vi;
  va_list va;
  u8 * choice;

  ASSERT (asn_app_attribute_is_oneof (a));
  is_single = a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice;
  /* Zero oneof value means clear attribute value. */
  is_clear = ! fmt;
  vi = 0;

  if (! is_clear)
    {
      va_start (va, fmt);
      choice = va_format (0, fmt, &va);
      va_end (va);
  
      /* Choice freed if non-needed. */
      vi = asn_app_add_oneof_attribute_helper (a, choice);
    }

  if (is_single)
    {
      if (is_clear)
        asn_app_invalidate_attribute (am, ai, i);
      else
        asn_app_set_attribute (am, ai, i, vi);
    }

  else if (a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_multiple_choice)
    {
      asn_app_attribute_type_t value_type = asn_app_attribute_value_type (a);
      if (! is_clear)
        asn_app_validate_attribute (am, ai, i);
      switch (value_type)
        {
        case ASN_APP_ATTRIBUTE_TYPE_u8:
          vec_validate (a->values.as_u8, i);
          if (is_clear)
            a->values.as_u8[i] = 0;
          else
            a->values.as_u8[i] |= 1 << vi;
          break;
        case ASN_APP_ATTRIBUTE_TYPE_u16:
          vec_validate (a->values.as_u16, i);
          if (is_clear)
            a->values.as_u16[i] = 0;
          else
            a->values.as_u16[i] |= 1 << vi;
          break;
        case ASN_APP_ATTRIBUTE_TYPE_u32:
          vec_validate (a->values.as_u32, i);
          if (is_clear)
            a->values.as_u32[i] = 0;
          else
            a->values.as_u32[i] |= 1 << vi;
          break;
        case ASN_APP_ATTRIBUTE_TYPE_u64:
          vec_validate (a->values.as_u64, i);
          if (is_clear)
            a->values.as_u64[i] = 0;
          else
            a->values.as_u64[i] |= (u64) 1 << (u64) vi;
          break;
        case ASN_APP_ATTRIBUTE_TYPE_bitmap:
          vec_validate (a->values.as_bitmap, i);
          if (is_clear)
            clib_bitmap_free (a->values.as_bitmap[i]);
          else
            a->values.as_bitmap[i] = clib_bitmap_ori (a->values.as_bitmap[i], vi);
          break;
        default:
          ASSERT (0);
          break;
        }
    }
}

u8 * asn_app_get_oneof_attribute (asn_app_attribute_main_t * am, u32 ai, u32 i)
{
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  ASSERT (a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_single_choice);
  asn_app_attribute_type_t vt = asn_app_attribute_value_type (a);

  if (! clib_bitmap_get (a->value_is_valid_bitmap, i))
    return 0;

  switch (vt)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      return vec_elt (a->oneof_values, a->values.as_u8[i]);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      return vec_elt (a->oneof_values, a->values.as_u16[i]);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      return vec_elt (a->oneof_values, a->values.as_u32[i]);
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      return vec_elt (a->oneof_values, a->values.as_u64[i]);
      break;
    default:
      ASSERT (0);
      break;
    }
  return 0;
}

uword * asn_app_get_oneof_attribute_multiple_choice_bitmap (asn_app_attribute_main_t * am, u32 ai, u32 i, uword * r)
{
  asn_app_attribute_t * a = vec_elt_at_index (am->attributes, ai);
  ASSERT (a->type == ASN_APP_ATTRIBUTE_TYPE_oneof_multiple_choice);
  asn_app_attribute_type_t vt = asn_app_attribute_value_type (a);

  clib_bitmap_zero (r);
  if (! clib_bitmap_get (a->value_is_valid_bitmap, i))
    return r;

  vec_validate (r, 0);
  switch (vt)
    {
    case ASN_APP_ATTRIBUTE_TYPE_u8:
      r[0] = a->values.as_u8[i];
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u16:
      r[0] = a->values.as_u16[i];
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u32:
      r[0] = a->values.as_u32[i];
      break;
    case ASN_APP_ATTRIBUTE_TYPE_u64:
      r[0] = a->values.as_u64[i];
      break;
    case ASN_APP_ATTRIBUTE_TYPE_bitmap:
      r = clib_bitmap_or (r, a->values.as_bitmap[i]);
      break;
    default:
      ASSERT (0);
      break;
    }
  return r;
}

static void asn_app_attribute_main_free (asn_app_attribute_main_t * am)
{
  asn_app_attribute_t * a;
  vec_foreach (a, am->attributes) asn_app_attribute_free (a);
  vec_free (am->attributes);
  hash_free (am->attribute_by_name);
  vec_free (am->attribute_map_for_unserialize);
}

void asn_app_user_type_free (asn_app_user_type_t * t)
{
  asn_user_type_free (&t->user_type);
  asn_app_attribute_main_free (&t->attribute_main);
}

void asn_app_main_free (asn_app_main_t * am)
{
  {
    asn_app_user_type_t * ut;
    asn_main_free (&am->asn_main);
    for (ut = am->user_types; ut < am->user_types + ARRAY_LEN (am->user_types); ut++)
      asn_app_user_type_free (ut);
  }
}

int asn_app_sort_message_by_increasing_time (asn_app_message_union_t * m0, asn_app_message_union_t * m1)
{
  f64 cmp = m0->header.time_stamp - m1->header.time_stamp;
  return cmp > 0 ? +1 : (cmp < 0 ? -1 : 0);
}

static void serialize_asn_app_text_message (serialize_main_t * m, va_list * va)
{
  asn_app_text_message_t * msg = va_arg (*va, asn_app_text_message_t *);
  vec_serialize (m, msg->text, serialize_vec_8);
}

static void unserialize_asn_app_text_message (serialize_main_t * m, va_list * va)
{
  asn_app_text_message_t * msg = va_arg (*va, asn_app_text_message_t *);
  vec_unserialize (m, &msg->text, unserialize_vec_8);
}

static void serialize_asn_app_photo_message (serialize_main_t * m, va_list * va)
{
  asn_app_photo_message_t * msg = va_arg (*va, asn_app_photo_message_t *);
  serialize (m, serialize_vec_asn_app_photo, &msg->photo, /* count */ 1);
}

static void unserialize_asn_app_photo_message (serialize_main_t * m, va_list * va)
{
  asn_app_photo_message_t * msg = va_arg (*va, asn_app_photo_message_t *);
  unserialize (m, unserialize_vec_asn_app_photo, &msg->photo, /* count */ 1);
}

static void serialize_asn_app_user_group_add_del_request_message (serialize_main_t * m, va_list * va)
{
  asn_app_user_group_add_del_request_message_t * msg = va_arg (*va, asn_app_user_group_add_del_request_message_t *);
  ASSERT (! msg);
}

static void unserialize_asn_app_user_group_add_del_request_message (serialize_main_t * m, va_list * va)
{
  asn_app_user_group_add_del_request_message_t * msg = va_arg (*va, asn_app_user_group_add_del_request_message_t *);
  ASSERT (! msg);
}

static void serialize_asn_app_message (serialize_main_t * m, va_list * va)
{
  asn_app_message_union_t * msg = va_arg (*va, asn_app_message_union_t *);
  u32 is_save_restore = va_arg (*va, u32);
  asn_app_message_header_t * h = &msg->header;

  /* User and time stamp will be copied to blob header and sent to ASN server. */
  serialize_likely_small_unsigned_integer (m, h->type);
  if (is_save_restore)
    {
      serialize_likely_small_unsigned_integer (m, h->from_user_index);
      serialize (m, serialize_f64, h->time_stamp);
    }

  switch (h->type)
    {
    case ASN_APP_MESSAGE_TYPE_text:
      serialize (m, serialize_asn_app_text_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_photo:
    case ASN_APP_MESSAGE_TYPE_video:
      serialize (m, serialize_asn_app_photo_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_user_group_add_del_request:
      serialize (m, serialize_asn_app_user_group_add_del_request_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_friend_request:
      /* nothing to do. */
      break;
    }
}

static void unserialize_asn_app_message (serialize_main_t * m, va_list * va)
{
  asn_app_message_union_t * msg = va_arg (*va, asn_app_message_union_t *);
  u32 is_save_restore = va_arg (*va, u32);
  asn_app_message_header_t * h = &msg->header;

  memset (msg, 0, sizeof (msg[0]));
  h->type = unserialize_likely_small_unsigned_integer (m);
  if (is_save_restore)
    {
      h->from_user_index = unserialize_likely_small_unsigned_integer (m);
      unserialize (m, unserialize_f64, &h->time_stamp);
    }

  switch (h->type)
    {
    case ASN_APP_MESSAGE_TYPE_text:
      unserialize (m, unserialize_asn_app_text_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_photo:
    case ASN_APP_MESSAGE_TYPE_video:
      unserialize (m, unserialize_asn_app_photo_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_user_group_add_del_request:
      unserialize (m, unserialize_asn_app_user_group_add_del_request_message, msg);
      break;

    case ASN_APP_MESSAGE_TYPE_friend_request:
      /* nothing to do. */
      break;

    default:
      serialize_error_return (m, "unknown message type 0x%x", msg->header.type);
      break;
    }
}

static void serialize_vec_asn_app_message (serialize_main_t * m, va_list * va)
{
  asn_app_message_union_t * msgs = va_arg (*va, asn_app_message_union_t *);
  u32 n = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n; i++)
    serialize (m, serialize_asn_app_message, &msgs[i], /* is_save_restore */ 1);
}

static void unserialize_vec_asn_app_message (serialize_main_t * m, va_list * va)
{
  asn_app_message_union_t * msgs = va_arg (*va, asn_app_message_union_t *);
  u32 n = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n; i++)
    unserialize (m, unserialize_asn_app_message, &msgs[i], /* is_save_restore */ 1);
}

static void
serialize_asn_app_gen_user (serialize_main_t * m, va_list * va)
{
  asn_app_gen_user_t * u = va_arg (*va, asn_app_gen_user_t *);
  asn_user_type_t * asn_ut = pool_elt (asn_user_type_pool, u->asn_user.user_type_index);
  asn_app_user_type_t * ut = CONTAINER_OF (asn_ut, asn_app_user_type_t, user_type);

  serialize (m, serialize_asn_user, &u->asn_user);
  vec_serialize (m, u->photos, serialize_vec_asn_app_photo);
  vec_serialize (m, u->messages_by_increasing_time, serialize_vec_asn_app_message);
  serialize (m, serialize_asn_app_attributes_for_index, &ut->attribute_main, u->asn_user.index);
}

static void
unserialize_asn_app_gen_user (serialize_main_t * m, va_list * va)
{
  asn_app_gen_user_t * u = va_arg (*va, asn_app_gen_user_t *);
  asn_user_type_t * asn_ut;
  asn_app_user_type_t * ut;
  unserialize (m, unserialize_asn_user, &u->asn_user);
  vec_unserialize (m, &u->photos, unserialize_vec_asn_app_photo);
  vec_unserialize (m, &u->messages_by_increasing_time, unserialize_vec_asn_app_message);

  asn_ut = pool_elt (asn_user_type_pool, u->asn_user.user_type_index);
  ut = CONTAINER_OF (asn_ut, asn_app_user_type_t, user_type);
  unserialize (m, unserialize_asn_app_attributes_for_index, &ut->attribute_main, u->asn_user.index);
}

static void
serialize_pool_asn_app_user (serialize_main_t * m, va_list * va)
{
  asn_app_user_t * u = va_arg (*va, asn_app_user_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      serialize (m, serialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void
unserialize_pool_asn_app_user (serialize_main_t * m, va_list * va)
{
  asn_app_user_t * u = va_arg (*va, asn_app_user_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      unserialize (m, unserialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void
serialize_pool_asn_app_user_group (serialize_main_t * m, va_list * va)
{
  asn_app_user_group_t * u = va_arg (*va, asn_app_user_group_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      serialize (m, serialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void
unserialize_pool_asn_app_user_group (serialize_main_t * m, va_list * va)
{
  asn_app_user_group_t * u = va_arg (*va, asn_app_user_group_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      unserialize (m, unserialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void
serialize_pool_asn_app_event (serialize_main_t * m, va_list * va)
{
  asn_app_event_t * u = va_arg (*va, asn_app_event_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      serialize (m, serialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void
unserialize_pool_asn_app_event (serialize_main_t * m, va_list * va)
{
  asn_app_event_t * u = va_arg (*va, asn_app_event_t *);
  u32 n_users = va_arg (*va, u32);
  u32 i;
  for (i = 0; i < n_users; i++)
    {
      unserialize (m, unserialize_asn_app_gen_user, &u[i].gen_user);
    }
}

static void serialize_asn_app_user_type (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_type_enum_t t = va_arg (*va, int);
  asn_app_user_type_t * ut;
  ASSERT (t < ARRAY_LEN (am->user_types));
  ut = am->user_types + t;
  serialize (m, serialize_asn_app_attribute_main, &ut->attribute_main);
  serialize (m, serialize_asn_user_type, &am->asn_main, &ut->user_type);
}

static void unserialize_asn_app_user_type (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_type_enum_t t = va_arg (*va, int);
  asn_app_user_type_t * ut;
  ASSERT (t < ARRAY_LEN (am->user_types));
  ut = am->user_types + t;
  unserialize (m, unserialize_asn_app_attribute_main, &ut->attribute_main);
  unserialize (m, unserialize_asn_user_type, &am->asn_main, &ut->user_type);
}

static char * asn_app_main_serialize_magic = "asn_app_main v0";

void
serialize_asn_app_main (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  int i;

  serialize_magic (m, asn_app_main_serialize_magic, strlen (asn_app_main_serialize_magic));
  serialize (m, serialize_asn_main, &am->asn_main);
  for (i = 0; i < ARRAY_LEN (am->user_types); i++)
    serialize (m, serialize_asn_app_user_type, am, i);
}

void
unserialize_asn_app_main (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  int i;

  unserialize_check_magic (m, asn_app_main_serialize_magic,
			   strlen (asn_app_main_serialize_magic),
			   "asn_app_main");
  unserialize (m, unserialize_asn_main, &am->asn_main);
  for (i = 0; i < ARRAY_LEN (am->user_types); i++)
    unserialize (m, unserialize_asn_app_user_type, am, i);
}

clib_error_t * asn_app_main_write_to_file (asn_app_main_t * am, char * unix_file)
{
  serialize_main_t m;
  clib_error_t * error;

  error = serialize_open_unix_file (&m, unix_file);
  if (error)
    return error;
  error = serialize (&m, serialize_asn_app_main, am);
  if (! error)
    serialize_close (&m);
  serialize_main_free (&m);
  return error;
}

clib_error_t * asn_app_main_read_from_file (asn_app_main_t * am, char * unix_file)
{
  serialize_main_t m;
  clib_error_t * error;

  error = unserialize_open_unix_file (&m, unix_file);
  if (error)
    return error;
  error = unserialize (&m, unserialize_asn_app_main, am);
  if (! error)
    unserialize_close (&m);
  unserialize_main_free (&m);
  return error;
}

static void
serialize_asn_app_profile_for_gen_user (serialize_main_t * m, va_list * va)
{
  asn_app_user_type_t * ut = va_arg (*va, asn_app_user_type_t *);
  asn_app_gen_user_t * u = va_arg (*va, asn_app_gen_user_t *);
  serialize (m, serialize_asn_app_profile_attributes_for_index, &ut->attribute_main, u->asn_user.index);
  vec_serialize (m, u->photos, serialize_vec_asn_app_photo);
}

static void
unserialize_asn_app_profile_for_gen_user (serialize_main_t * m, va_list * va)
{
  asn_app_user_type_t * ut = va_arg (*va, asn_app_user_type_t *);
  asn_app_gen_user_t * u = va_arg (*va, asn_app_gen_user_t *);
  unserialize (m, unserialize_asn_app_profile_attributes_for_index, &ut->attribute_main, u->asn_user.index);
  vec_unserialize (m, &u->photos, unserialize_vec_asn_app_photo);
}

static void
serialize_asn_app_profile_for_user (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_t * u = va_arg (*va, asn_app_user_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_user];
  serialize_magic (m, ut->user_type.name, strlen (ut->user_type.name));
  serialize (m, serialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void
unserialize_asn_app_profile_for_user (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_t * u = va_arg (*va, asn_app_user_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_user];
  unserialize_check_magic (m, ut->user_type.name, strlen (ut->user_type.name), "asn_app_user_for_profile");
  unserialize (m, unserialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void
serialize_asn_app_profile_for_user_group (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_group_t * u = va_arg (*va, asn_app_user_group_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_user_group];
  serialize_magic (m, ut->user_type.name, strlen (ut->user_type.name));
  serialize (m, serialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void
unserialize_asn_app_profile_for_user_group (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_user_group_t * u = va_arg (*va, asn_app_user_group_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_user_group];
  unserialize_check_magic (m, ut->user_type.name, strlen (ut->user_type.name), "asn_app_user_group_for_profile");
  unserialize (m, unserialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void
serialize_asn_app_profile_for_event (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_event_t * u = va_arg (*va, asn_app_event_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_event];
  serialize_magic (m, ut->user_type.name, strlen (ut->user_type.name));
  serialize (m, serialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void
unserialize_asn_app_profile_for_event (serialize_main_t * m, va_list * va)
{
  asn_app_main_t * am = va_arg (*va, asn_app_main_t *);
  asn_app_event_t * u = va_arg (*va, asn_app_event_t *);
  asn_app_user_type_t * ut = &am->user_types[ASN_APP_USER_TYPE_event];
  unserialize_check_magic (m, ut->user_type.name, strlen (ut->user_type.name), "asn_app_event_for_profile");
  unserialize (m, unserialize_asn_app_profile_for_gen_user, ut, &u->gen_user);
}

static void asn_app_free_user (asn_user_t * au)
{
  asn_app_user_t * u = CONTAINER_OF (au, asn_app_user_t, gen_user.asn_user);
  asn_app_user_free (u);
}

static void asn_app_free_user_group (asn_user_t * au)
{
  asn_app_user_group_t * u = CONTAINER_OF (au, asn_app_user_group_t, gen_user.asn_user);
  asn_app_user_group_free (u);
}

static void asn_app_free_event (asn_user_t * au)
{
  asn_app_event_t * u = CONTAINER_OF (au, asn_app_user_group_t, gen_user.asn_user);
  asn_app_event_free (u);
}

static char * asn_app_user_blob_name = "asn_app_user";

void asn_app_user_update_blob (asn_app_main_t * app_main, asn_app_user_type_enum_t user_type, u32 user_index)
{
  asn_main_t * am = &app_main->asn_main;
  asn_app_user_type_t * app_ut;
  asn_user_type_t * ut;
  asn_client_socket_t * cs;
  serialize_main_t m;
  void * au;
  u8 * v;
  clib_error_t * error;

  ASSERT (user_type < ARRAY_LEN (app_main->user_types));
  app_ut = app_main->user_types + user_type;
  ut = &app_ut->user_type;

  ASSERT (! pool_is_free_index (ut->user_pool, user_index));
  au = ut->user_pool + user_index * ut->user_type_n_bytes;

  serialize_open_vector (&m, 0);
  serialize_likely_small_unsigned_integer (&m, user_type);
  error = serialize (&m, app_ut->serialize_blob_contents, app_main, au);
  if (error)
    clib_error_report (error);
  v = serialize_close_vector (&m);

  vec_foreach (cs, am->client_sockets)
    {
      asn_socket_t * as = asn_socket_at_index (am, cs->socket_index);
      error = asn_socket_exec (am, as, 0, "blob%c%s%c-%c%c%v", 0, asn_app_user_blob_name, 0, 0, 0, v);
      if (error)
        clib_error_report (error);
    }

  vec_free (v);
}

/* Handler for blobs written with previous function. */
static clib_error_t *
asn_app_user_blob_handler (asn_main_t * am, asn_socket_t * as, asn_pdu_blob_t * blob, u32 n_bytes_in_pdu)
{
  clib_error_t * error = 0;
  asn_user_t * au = asn_user_with_encrypt_key (am, ASN_TX, blob->owner);
  asn_app_main_t * app_main = CONTAINER_OF (am, asn_app_main_t, asn_main);
  void * app_user;
  asn_app_user_type_t * ut;
  serialize_main_t m;

  if (! au)
    {
      error = clib_error_return (0, "owner user not found %U", format_hex_bytes, blob->owner, sizeof (blob->owner));
      goto done;
    }

  /* Never update blob for self user. */
  if (asn_is_user_for_ref (au, &am->self_user_ref))
    goto done;

  serialize_open_data (&m, asn_pdu_contents_for_blob (blob), asn_pdu_n_content_bytes_for_blob (blob, n_bytes_in_pdu));

  /* User type from blob. */
  {
    asn_app_user_type_enum_t t = unserialize_likely_small_unsigned_integer (&m);

    if (t >= ARRAY_LEN (app_main->user_types))
      {
        error = clib_error_return (0, "unknown user type 0x%x", t);
        goto done;
      }
    ut = app_main->user_types + t;
  }

  app_user = (void *) au - ut->user_type.user_type_offset_of_asn_user;
  error = unserialize (&m, ut->unserialize_blob_contents, app_main, app_user);
  serialize_close (&m);

  if (! error && ut->did_update_user)
    ut->did_update_user (au);

 done:
  /* FIXME */
  if (error)
    clib_warning ("%U", format_clib_error, error);

  return error;
}

static void asn_app_gen_user_add_message (asn_app_gen_user_t * gu, asn_app_message_union_t * msg)
{
  word i, l;

  l = vec_len (gu->messages_by_increasing_time);
  if (l > 0)
    {
      for (i = l - 1; i >= 0; i--)
        if (msg->header.time_stamp > gu->messages_by_increasing_time[i].header.time_stamp)
          break;
      if (i == l - 1)
        vec_add1 (gu->messages_by_increasing_time, msg[0]);
      else
        {
          vec_insert (gu->messages_by_increasing_time, 1, i);
          gu->messages_by_increasing_time[i] = msg[0];
        }
    }
  else
    vec_add1 (gu->messages_by_increasing_time, msg[0]);
}

static clib_error_t * rx_message (asn_main_t * am,
                                  asn_user_t * au,
                                  asn_app_message_union_t * msg)
{
  clib_error_t * error = 0;
  asn_app_main_t * app_main = CONTAINER_OF (am, asn_app_main_t, asn_main);
  asn_user_type_t * ut = pool_elt (asn_user_type_pool, au->user_type_index);
  asn_app_user_type_t * app_ut = CONTAINER_OF (ut, asn_app_user_type_t, user_type);
  asn_app_user_type_enum_t te = app_ut - app_main->user_types;
    
  switch (te)
    {
    case ASN_APP_USER_TYPE_user: {
      asn_app_user_t * u = CONTAINER_OF (au, asn_app_user_t, gen_user.asn_user);
      asn_app_gen_user_add_message (&u->gen_user, msg);
      break;
    }

    case ASN_APP_USER_TYPE_user_group: {
      asn_app_user_group_t * g = CONTAINER_OF (au, asn_app_user_group_t, gen_user.asn_user);
      asn_app_gen_user_add_message (&g->gen_user, msg);
      break;
    }

    case ASN_APP_USER_TYPE_event: {
      asn_app_event_t * e = CONTAINER_OF (au, asn_app_event_t, gen_user.asn_user);
      asn_app_gen_user_add_message (&e->gen_user, msg);
      break;
    }

    default:
      ASSERT (0);
      error = clib_error_return (0, "unknown user type %d", te);
      goto done;
    }

  if (app_ut->did_add_message)
    app_ut->did_add_message (au);

 done:
  return error;
}

static clib_error_t *
asn_unnamed_blob_handler (asn_main_t * am, asn_socket_t * as,
                          asn_pdu_blob_t * blob, u32 n_bytes_in_pdu)
{
  clib_error_t * error = 0;
  asn_user_t * dst_au = asn_user_with_encrypt_key (am, ASN_TX, blob->owner);
  asn_user_t * src_au = asn_user_with_encrypt_key (am, ASN_TX, blob->author);
  serialize_main_t m;
  asn_app_message_union_t msg;

  memset (&msg, 0, sizeof (msg));

  if (! dst_au || ! src_au)
    {
      error = clib_error_return (0, "owner/author user not found");
      goto done;
    }

  serialize_open_data (&m, asn_pdu_contents_for_blob (blob), asn_pdu_n_content_bytes_for_blob (blob, n_bytes_in_pdu));
  error = unserialize (&m, unserialize_asn_app_message, &msg, /* is_save_restore */ 0);
  serialize_close (&m);
  if (error)
    goto done;

  msg.header.from_user_index = src_au->index;
  msg.header.time_stamp = 1e-9 * clib_net_to_host_u64 (blob->time_stamp_in_nsec_from_1970);

  rx_message (am, src_au, &msg);

 done:
  if (error)
    asn_app_message_union_free (&msg);
  return error;
}

clib_error_t *
asn_app_send_text_message_to_user (asn_app_main_t * app_main,
                                   asn_app_user_type_enum_t to_user_type,
                                   u32 to_user_index,
                                   char * fmt, ...)
{
  clib_error_t * error = 0;
  asn_main_t * am = &app_main->asn_main;
  asn_app_user_type_t * app_ut;
  asn_user_type_t * ut;
  asn_user_t * to_asn_user;
  asn_client_socket_t * cs;
  asn_app_message_union_t msg;
  u8 * content = 0;
  va_list va;

  ASSERT (to_user_type < ARRAY_LEN (app_main->user_types));
  app_ut = app_main->user_types + to_user_type;
  ut = &app_ut->user_type;

  {
    asn_user_ref_t r = {
      .user_index = to_user_index,
      .type_index = ut->index,
    };

    to_asn_user = asn_user_by_ref (&r);
    if (! to_asn_user)
      {
        error = clib_error_return (0, "unknown user with type %U and index %d",
                                   format_asn_app_user_type_enum, to_user_type,
                                   to_user_index);
        goto done;
      }
  }

  memset (&msg, 0, sizeof (msg));

  va_start (va, fmt);
  msg.header.type = ASN_APP_MESSAGE_TYPE_text;
  msg.header.from_user_index = am->self_user_ref.user_index;
  msg.header.time_stamp = unix_time_now ();
  msg.text.text = va_format (0, fmt, &va);
  va_end (va);

  {
    serialize_main_t sm;
    serialize_open_vector (&sm, 0);
    error = serialize (&sm, serialize_asn_app_message, &msg, /* is_save_restore */ 0);
    if (error)
      goto done;
    content = serialize_close_vector (&sm);
  }

  error = rx_message (am, to_asn_user, &msg);
  if (error)
    goto done;

  vec_foreach (cs, am->client_sockets)
    {
      asn_socket_t * as = asn_socket_at_index (am, cs->socket_index);
      error = asn_socket_exec (am, as, 0, "blob%c~%U%c-%c%c%v", 0,
                               format_hex_bytes, to_asn_user->crypto_keys.public.encrypt_key, sizeof (to_asn_user->crypto_keys.public.encrypt_key),
                               0, 0, 0,
                               content);
      if (error)
        goto done;
    }

 done:
  vec_free (content);
  return error;
}

void asn_app_main_init (asn_app_main_t * am)
{
  asn_set_blob_handler_for_name (&am->asn_main, asn_unnamed_blob_handler, "");
  asn_set_blob_handler_for_name (&am->asn_main, asn_app_user_blob_handler, "%s", asn_app_user_blob_name);

  if (! am->user_types[ASN_APP_USER_TYPE_user].user_type.was_registered)
    {
      asn_app_user_type_t t = {
        .user_type = {
          .name = "actual user",
          .user_type_n_bytes = sizeof (asn_app_user_t),
          .user_type_offset_of_asn_user = STRUCT_OFFSET_OF (asn_app_user_t, gen_user.asn_user),
          .free_user = asn_app_free_user,
          .serialize_pool_users = serialize_pool_asn_app_user,
          .unserialize_pool_users = unserialize_pool_asn_app_user,
        },
        .serialize_blob_contents = serialize_asn_app_profile_for_user,
        .unserialize_blob_contents = unserialize_asn_app_profile_for_user,
      };

      am->user_types[ASN_APP_USER_TYPE_user] = t;
      asn_register_user_type (&am->user_types[ASN_APP_USER_TYPE_user].user_type);
    }

  if (! am->user_types[ASN_APP_USER_TYPE_user_group].user_type.was_registered)
    {
      asn_app_user_type_t t = {
        .user_type = {
          .name = "user group",
          .user_type_n_bytes = sizeof (asn_app_user_group_t),
          .user_type_offset_of_asn_user = STRUCT_OFFSET_OF (asn_app_user_group_t, gen_user.asn_user),
          .free_user = asn_app_free_user_group,
          .serialize_pool_users = serialize_pool_asn_app_user_group,
          .unserialize_pool_users = unserialize_pool_asn_app_user_group,
        },
        .serialize_blob_contents = serialize_asn_app_profile_for_user_group,
        .unserialize_blob_contents = unserialize_asn_app_profile_for_user_group,
      };

      am->user_types[ASN_APP_USER_TYPE_user_group] = t;
      asn_register_user_type (&am->user_types[ASN_APP_USER_TYPE_user_group].user_type);
    }

  if (! am->user_types[ASN_APP_USER_TYPE_event].user_type.was_registered)
    {
      asn_app_user_type_t t = {
        .user_type = {
          .name = "event",
          .user_type_n_bytes = sizeof (asn_app_event_t),
          .user_type_offset_of_asn_user = STRUCT_OFFSET_OF (asn_app_event_t, gen_user.asn_user),
          .free_user = asn_app_free_event,
          .serialize_pool_users = serialize_pool_asn_app_event,
          .unserialize_pool_users = unserialize_pool_asn_app_event,
        },
        .serialize_blob_contents = serialize_asn_app_profile_for_event,
        .unserialize_blob_contents = unserialize_asn_app_profile_for_event,
      };

      am->user_types[ASN_APP_USER_TYPE_event] = t;
      asn_register_user_type (&am->user_types[ASN_APP_USER_TYPE_event].user_type);
    }
}
