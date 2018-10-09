/*
 * Copyright (c) 2016 FlyLab
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/*
 
 Original code from:
 https://github.com/liteserver/binn
 
 This code is intended to be crossplatform & with no dependencies -event from GroundBase !
 For now, only Commons.h is used from GroundBase, for debbuging purposes.
 
 
 Changes from orinal code:
 - removed C11 anonymous union.
 - added const pointers references in read only methods ("getters")
 
 Futures changes : 
 - remove gotos 
 */
#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <string.h>
//#include <memory.h>
#include "binn.h"

#define UNUSED(x) (void)(x)

// magic number:  0x1F 0xb1 0x22 0x1F  =>  0x1FB1221F or 0x1F22B11F
// because the BINN_STORAGE_NOBYTES (binary 000) may not have so many sub-types (BINN_STORAGE_HAS_MORE = 0x10)
#define BINN_MAGIC            0x00001F2B //0x1F22B11F

#define MAX_BINN_HEADER       9  // [1:type][4:size][4:count]
#define MIN_BINN_SIZE         3  // [1:type][1:size][1:count]
#define CHUNK_SIZE            256  // 1024

#define BINN_STRUCT        1
#define BINN_BUFFER        2

void* (*malloc_fn)(size_t len) = 0;
void* (*realloc_fn)(void *ptr, size_t len) = 0;
void  (*free_fn)(void *ptr) = 0;

/***************************************************************************/

#ifdef WIN32
    #define __LITTLE_ENDIAN  1234
    #define __BIG_ENDIAN     4321
    #define __BYTE_ORDER   __LITTLE_ENDIAN
#elif defined ARDUINO
    #define __LITTLE_ENDIAN  1234
    #define __BIG_ENDIAN     4321
    #define __BYTE_ORDER   __LITTLE_ENDIAN
#else
    #ifndef __BYTE_ORDER
    // on android we avoid the inclusion of htonx functions disabling the processing of _SYS_ENDIAN_H_
        #define _SYS_ENDIAN_H_
        #define _LITTLE_ENDIAN   1234
        #define _BIG_ENDIAN      4321
        #include <machine/endian.h>
        #define __LITTLE_ENDIAN _LITTLE_ENDIAN
        #define __BIG_ENDIAN    _BIG_ENDIAN
        #define __BYTE_ORDER    _BYTE_ORDER
    #endif
#endif

#ifndef __BYTE_ORDER
#error "__BYTE_ORDER not defined"
#endif
#ifndef __BIG_ENDIAN
#error "__BIG_ENDIAN not defined"
#endif
#ifndef __LITTLE_ENDIAN
#error "__LITTLE_ENDIAN not defined"
#endif
#if __BIG_ENDIAN == __LITTLE_ENDIAN
#error "__BIG_ENDIAN == __LITTLE_ENDIAN"
#endif

#undef htons
#undef htonl
#undef ntohs
#undef ntohl

BINN_PRIVATE unsigned short htons(unsigned short input);
BINN_PRIVATE unsigned int htonl(unsigned int input);

BINN_PRIVATE unsigned short htons(unsigned short input) {
#if __BYTE_ORDER == __BIG_ENDIAN
  return input;
#else
  unsigned short result;
  unsigned char *source = (unsigned char *) &input;
  unsigned char *dest = (unsigned char *) &result;

  dest[0] = source[1];
  dest[1] = source[0];

  return result;
#endif
}

BINN_PRIVATE unsigned int htonl(unsigned int input) {
#if __BYTE_ORDER == __BIG_ENDIAN
  return input;
#else
  unsigned int result;
  unsigned char *source = (unsigned char *) &input;
  unsigned char *dest = (unsigned char *) &result;

  dest[0] = source[3];
  dest[1] = source[2];
  dest[2] = source[1];
  dest[3] = source[0];

  return result;
#endif
}
#ifndef htonll
BINN_PRIVATE uint64 htonll(uint64 input) {
#if __BYTE_ORDER == __BIG_ENDIAN
  return input;
#else
  uint64 result;
  unsigned char *source = (unsigned char *) &input;
  unsigned char *dest = (unsigned char *) &result;
  int i;

  for (i=0; i < 8; i++) {
    dest[i] = source[7-i];
  }

  return result;
#endif
}
#endif /*#ifndef htonll*/

#define ntohs htons
#define ntohl htonl

#ifdef ntohll
#undef ntohll
#endif

#define ntohll htonll

/***************************************************************************/

#ifndef WIN32
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

/***************************************************************************/


/* Prototypes */

BINN_PRIVATE void * compress_int(int *pstorage_type, int *ptype, void *psource);
BINN_PRIVATE void check_alloc_functions(void);
BINN_PRIVATE void * binn_malloc(int size);
BINN_PRIVATE void * binn_memdup(void *src, int size);
BINN_PRIVATE size_t strlen2(char *str);
binn * APIENTRY binn_list(void);
binn * APIENTRY binn_map(void);
binn * APIENTRY binn_object(void);
BINN_PRIVATE int CalcAllocation(int needed_size, int alloc_size);
BINN_PRIVATE unsigned char * SearchForID(unsigned char *p, int header_size, int size, int numitems, int id);
BINN_PRIVATE int binn_get_ptr_type(const void *ptr);
BINN_PRIVATE uint8_t CheckAllocation(binn *item, int add_size);
BINN_PRIVATE unsigned char * SearchForKey(unsigned char *p, int header_size, int size, int numitems, const char *key);
BINN_PRIVATE uint8_t binn_list_add_raw(binn *item, int type, void *pvalue, int size);
BINN_PRIVATE uint8_t binn_object_set_raw(binn *item, char *key, int type, void *pvalue, int size) ;
BINN_PRIVATE uint8_t binn_map_set_raw(binn *item, int id, int type, void *pvalue, int size);
BINN_PRIVATE unsigned char * AdvanceDataPos(unsigned char *p);
BINN_PRIVATE uint8_t binn_save_header(binn *item);
BINN_PRIVATE uint8_t IsValidBinnHeader(const void *pbuf, int *ptype, int *pcount, int *psize, int *pheadersize);
BINN_PRIVATE int binn_buf_type(const void *pbuf);
BINN_PRIVATE int binn_buf_count(void *pbuf);
BINN_PRIVATE int binn_buf_size(void *pbuf);
//BINN_PRIVATE uint8_t is_bool_str(char *str, uint8_t *pbool);
BINN_PRIVATE uint8_t is_float(char *p);
BINN_PRIVATE uint8_t copy_value(void *psource, void *pdest, int source_type, int dest_type, int data_store);
BINN_PRIVATE binn * binn_alloc_item(void);
BINN_PRIVATE uint8_t copy_float_value(void *psource, void *pdest, int source_type, int dest_type);
BINN_PRIVATE void zero_value(void *pvalue, int type);
BINN_PRIVATE uint8_t is_integer(char *p);
uint8_t APIENTRY binn_add_value(binn *item, int binn_type, int id, char *name, int type, void *pvalue, int size);
BINN_PRIVATE uint8_t copy_int_value(void *psource, void *pdest, int source_type, int dest_type);
BINN_PRIVATE int type_family(int type);
BINN_PRIVATE uint8_t GetValue(unsigned char *p, binn *value);
BINN_PRIVATE uint8_t copy_raw_value(void *psource, void *pdest, int data_store);
BINN_PRIVATE int int_type(int type);
BINN_PRIVATE uint8_t GetWriteConvertedData(int *ptype, void **ppvalue, int *psize);
BINN_PRIVATE uint8_t binn_read_next_pair(int expected_type, binn_iter *iter, int *pid, char *pkey, binn *value);
BINN_PRIVATE uint8_t binn_read_pair(int expected_type, void *ptr, int pos, int *pid, char *pkey, binn *value);

#ifdef _MSC_VER
#define atoi64 _atoi64
#else
int64 atoi64(char *str);
#endif

/***************************************************************************/

void APIENTRY binn_set_alloc_functions(void* (*new_malloc)(size_t), void* (*new_realloc)(void*,size_t), void (*new_free)(void*)) {

  malloc_fn = new_malloc;
  realloc_fn = new_realloc;
  free_fn = new_free;

}

/***************************************************************************/

BINN_PRIVATE void check_alloc_functions()
{

  if (malloc_fn == 0) malloc_fn = &malloc;
  if (realloc_fn == 0) realloc_fn = &realloc;
  if (free_fn == 0) free_fn = &free;

}

/***************************************************************************/

BINN_PRIVATE void * binn_malloc(int size)
{
  check_alloc_functions();
  return malloc_fn((size_t)size);
}

/***************************************************************************/

BINN_PRIVATE void * binn_memdup(void *src, int size)
{
  void *dest;

  if (src == NULL || size <= 0) return NULL;
  dest = binn_malloc(size);
  if (dest == NULL) return NULL;
  memcpy(dest, src, size);
  return dest;

}

/***************************************************************************/

BINN_PRIVATE size_t strlen2(char *str) {

  if (str == NULL) return 0;
  return strlen(str);

}

/***************************************************************************/

int APIENTRY binn_create_type(int storage_type, int data_type_index) {
  if (data_type_index < 0) return -1;
  if ((storage_type < BINN_STORAGE_MIN) || (storage_type > BINN_STORAGE_MAX)) return -1;
  if (data_type_index < 16)
    return storage_type | data_type_index;
  else if (data_type_index < 4096) {
    storage_type |= BINN_STORAGE_HAS_MORE;
    storage_type <<= 8;
    data_type_index >>= 4;
    return storage_type | data_type_index;
  } else
    return -1;
}

/***************************************************************************/

uint8_t APIENTRY binn_get_type_info(int long_type, int *pstorage_type, int *pextra_type)
{
    int storage_type  = -1;
    int extra_type = -1;
  
    uint8_t retval = 1;

again:

    if (long_type < 0)
    {
//        #warning goto use
        goto loc_invalid;
    }
    else if (long_type <= 0xff)
    {
        storage_type = long_type & BINN_STORAGE_MASK;
        extra_type = long_type & BINN_TYPE_MASK;
    }
    else if (long_type <= 0xffff)
    {
        storage_type = long_type & BINN_STORAGE_MASK16;
        storage_type >>= 8;
        extra_type = long_type & BINN_TYPE_MASK16;
        extra_type >>= 4;
    }
    else if (long_type & BINN_STORAGE_VIRTUAL)
    {
        //storage_type = BINN_STORAGE_VIRTUAL;
        //extra_type = xxx;
        long_type &= 0xffff;
//        #warning goto use
        goto again;
    }
    else
    {
        loc_invalid:
        storage_type = -1;
        extra_type = -1;
        retval = 0;
    }

    if (pstorage_type)
    {
        *pstorage_type = storage_type;
    }
    if (pextra_type)
    {
        *pextra_type = extra_type;
    }

  return retval;

}

/***************************************************************************/

uint8_t APIENTRY binn_create(binn *item, int type, int size, void *pointer)
{
    uint8_t retval = 1;

    switch (type)
    {
        case BINN_LIST:
        case BINN_MAP:
        case BINN_OBJECT:
            break;
            
        default:
            return retval;
    }

    if ((item == NULL) || (size < 0))
    {
        return retval;
    }
    if (size < MIN_BINN_SIZE)
    {
        if (pointer)
        {
            return retval;
        }
        else
        {
            size = 0;
        }
    }

    memset(item, 0, sizeof(binn));

    if (pointer)
    {
        item->pre_allocated = 1;
        item->pbuf = pointer;
        item->alloc_size = size;
    }
    else
    {
        item->pre_allocated = 0;
    
        if (size == 0)
            size = CHUNK_SIZE;
        
        pointer = binn_malloc(size);
        
        if (pointer == 0)
        {
            return INVALID_BINN;
        }
        
        item->pbuf = pointer;
        item->alloc_size = size;
    }

    item->header = BINN_MAGIC;
    //item->allocated = 0;   -- already zeroed
    
    item->writable = 1;
    item->used_size = MAX_BINN_HEADER;  // save space for the header
    item->type = type;
    //item->count = 0;           -- already zeroed
    item->dirty = 1;          // the header is not written to the buffer

    retval = 1;

//loc_exit:
  return retval;

}

/***************************************************************************/

binn * APIENTRY binn_new(int type, int size, void *pointer) {
  binn *item;

  item = (binn*) binn_malloc(sizeof(binn));

  if (binn_create(item, type, size, pointer) == 0)
  {
    free_fn(item);
    return NULL;
  }

  item->allocated = 1;
  return item;

}

/*************************************************************************************/

uint8_t APIENTRY binn_create_list(binn *list) {

  return binn_create(list, BINN_LIST, 0, NULL);

}

/*************************************************************************************/

uint8_t APIENTRY binn_create_map(binn *map) {

  return binn_create(map, BINN_MAP, 0, NULL);

}

/*************************************************************************************/

uint8_t APIENTRY binn_create_object(binn *object) {

  return binn_create(object, BINN_OBJECT, 0, NULL);

}

/***************************************************************************/



binn * APIENTRY binn_list() {
  return binn_new(BINN_LIST, 0, 0);
}

/***************************************************************************/

binn * APIENTRY binn_map() {
  return binn_new(BINN_MAP, 0, 0);
}

/***************************************************************************/

binn * APIENTRY binn_object() {
  return binn_new(BINN_OBJECT, 0, 0);
}

/*************************************************************************************/

uint8_t APIENTRY binn_load(const void *data, binn *value) {

  if ((data == NULL) || (value == NULL))
      return 0;
    
  memset(value, 0, sizeof(binn));
  value->header = BINN_MAGIC;
  //value->allocated = 0;  --  already zeroed
  //value->writable = 0;

  if (binn_is_valid( BINN_REMOVE_CONST(void*) data, &value->type, &value->count, &value->size) == 0)
      return 0;
    
  value->ptr = BINN_REMOVE_CONST(void*) data;
  return 1;

}

/*************************************************************************************/

binn * APIENTRY binn_open(const void *data) {
  binn *item;

  item = (binn*) binn_malloc(sizeof(binn));

  if (binn_load(data, item) == 0) {
    free_fn(item);
    return NULL;
  }

  item->allocated = 1;
  return item;

}

/***************************************************************************/


BINN_PRIVATE int binn_get_ptr_type(const void *ptr) {

  if (ptr == NULL) return 0;

  switch (*(unsigned int *)ptr) {
  case BINN_MAGIC:
    return BINN_STRUCT;
  default:
    return BINN_BUFFER;
  }

}

/***************************************************************************/

uint8_t APIENTRY binn_is_struct(void *ptr) {

  if (ptr == NULL) return 0;

  if ((*(unsigned int *)ptr) == BINN_MAGIC) {
    return 1;
  } else {
    return 0;
  }

}

/***************************************************************************/

BINN_PRIVATE int CalcAllocation(int needed_size, int alloc_size) {
  int calc_size;

  calc_size = alloc_size;
  while (calc_size < needed_size) {
    calc_size <<= 1;  // same as *= 2
    //calc_size += CHUNK_SIZE;  -- this is slower than the above line, because there are more reallocations
  }
  return calc_size;

}

/***************************************************************************/

BINN_PRIVATE uint8_t CheckAllocation(binn *item, int add_size) {
  int  alloc_size;
  void *ptr;

  if (item->used_size + add_size > item->alloc_size)
  {
      if (item->pre_allocated)
      {
          return 0;
      }
      alloc_size = CalcAllocation(item->used_size + add_size, item->alloc_size);
      
      ptr = realloc_fn(item->pbuf,(size_t) alloc_size);
      
      if (ptr == NULL)
      {
          return 0;
      }
      item->pbuf = ptr;
      item->alloc_size = alloc_size;
  }

  return 1;
}

/***************************************************************************/

#if __BYTE_ORDER == __BIG_ENDIAN

BINN_PRIVATE int get_storage_size(int storage_type) {

  switch (storage_type) {
  case BINN_STORAGE_NOBYTES:
    return 0;
  case BINN_STORAGE_BYTE:
    return 1;
  case BINN_STORAGE_WORD:
    return 2;
  case BINN_STORAGE_DWORD:
    return 4;
  case BINN_STORAGE_QWORD:
    return 8;
  default:
    return 0;
  }

}

#endif

/***************************************************************************/

BINN_PRIVATE unsigned char * AdvanceDataPos(unsigned char *p) {
  unsigned char byte;
  int  storage_type, DataSize;

  byte = *p; p++;
  storage_type = byte & BINN_STORAGE_MASK;
  if (byte & BINN_STORAGE_HAS_MORE) p++;

  switch (storage_type) {
  case BINN_STORAGE_NOBYTES:
    //p += 0;
    break;
  case BINN_STORAGE_BYTE:
    p ++;
    break;
  case BINN_STORAGE_WORD:
    p += 2;
    break;
  case BINN_STORAGE_DWORD:
    p += 4;
    break;
  case BINN_STORAGE_QWORD:
    p += 8;
    break;
  case BINN_STORAGE_BLOB:
    DataSize = *((int *)p);
    DataSize = (int) ntohl((unsigned int) DataSize);
    p += 4 + DataSize;
    break;
  case BINN_STORAGE_CONTAINER:
    DataSize = *((unsigned char*)p);
    if (DataSize & 0x80) {
      DataSize = *((int*)p);
      DataSize =  (int) ntohl((unsigned int) DataSize);
      DataSize &= 0x7FFFFFFF;
    }
    DataSize--;  // remove the type byte already added before
    p += DataSize;
    break;
  case BINN_STORAGE_STRING:
    DataSize = *((unsigned char*)p);
    if (DataSize & 0x80) {
      DataSize = *((int*)p); p+=4;
      DataSize =  (int) ntohl((unsigned int) DataSize);
      DataSize &= 0x7FFFFFFF;
    } else {
      p++;
    }
    p += DataSize;
    p++;  // null terminator.
    break;
  default:
    return 0;
  }

  return p;

}

/***************************************************************************/

BINN_PRIVATE unsigned char * SearchForID(unsigned char *p, int header_size, int size, int numitems, int id) {
  unsigned char *plimit;
  int  i, int32;

  plimit = p + size - 1;
  p += header_size;

  // search for the ID in all the arguments.
  for (i = 0; i < numitems; i++) {
    int32 = *((int*)p); p += 4;
    int32 =(int) ntohl( (unsigned int)int32);
    if (p > plimit) break;
    // Compare if the IDs are equal.
    if (int32 == id) return p;
    // xxx
    p = AdvanceDataPos(p);
    if ((p == 0) || (p > plimit)) break;
  }

  return NULL;

}

/***************************************************************************/

BINN_PRIVATE unsigned char * SearchForKey(unsigned char *p, int header_size, int size, int numitems, const char *key) {
  unsigned char len, *plimit;
  int  i, keylen;

  plimit = p + size - 1;
  p += header_size;

  keylen = (int)strlen(key);

  // search for the key in all the arguments.
  for (i = 0; i < numitems; i++) {
    len = *((unsigned char *)p);
    p++;
    if (p > plimit) break;
    // Compare if the strings are equal.
    if (len > 0) {
      if (strnicmp((char*)p, key, len) == 0) {   // note that there is no null terminator here
        if (keylen == len) {
          p += len;
          return p;
        }
      }
      p += len;
      if (p > plimit) break;
    } else if (len == keylen) {   // in the case of empty string: ""
      return p;
    }
    // xxx
    p = AdvanceDataPos(p);
    if ((p == 0) || (p > plimit)) break;
  }

  return NULL;

}

/***************************************************************************/

BINN_PRIVATE uint8_t AddValue(binn *item, int type, void *pvalue, int size);

/***************************************************************************/

BINN_PRIVATE uint8_t binn_list_add_raw(binn *item, int type, void *pvalue, int size) {

    if ((item == NULL) || (item->type != BINN_LIST) || (item->writable == 0))
    {
        return 0;
    }

  //if (CheckAllocation(item, 4) == 0) return 0;  // 4 bytes used for data_store and data_format.

  if (AddValue(item, type, pvalue, size) == 0)
      return 0;

  item->count++;

  return 1;

}

/***************************************************************************/

BINN_PRIVATE uint8_t binn_object_set_raw(binn *item, char *key, int type, void *pvalue, int size)
{
  unsigned char *p, len;
  int int32;

  if ((item == NULL) || (item->type != BINN_OBJECT) || (item->writable == 0))
      return 0;

  if (key == NULL)
      return 0;
    
  int32 = (int) strlen(key);
    
  if (int32 > 255)
      return 0;

  // is the key already in it?
  p = SearchForKey(item->pbuf, MAX_BINN_HEADER, item->used_size, item->count, key);
    
  if (p)
      return 0;

  // start adding it

  if (CheckAllocation(item, 1 + int32) == 0)
      return 0;  // bytes used for the key size and the key itself.

  p = ((unsigned char *) item->pbuf) + item->used_size;
  len = (unsigned char) int32;
  *p = len;
  p++;
  memcpy(p, key, int32);
  int32++;  // now contains the strlen + 1 byte for the len
  item->used_size += int32;

  if (AddValue(item, type, pvalue, size) == 0) {
    item->used_size -= int32;
    return 0;
  }

  item->count++;

  return 1;

}

/***************************************************************************/

BINN_PRIVATE uint8_t binn_map_set_raw(binn *item, int id, int type, void *pvalue, int size)
{
  unsigned char *p;
  int int32;

  if ((item == NULL) || (item->type != BINN_MAP) || (item->writable == 0)) return 0;

  // is the ID already in it?
  p = SearchForID(item->pbuf, MAX_BINN_HEADER, item->used_size, item->count, id);
  
    if (p)
    {
        return 0;
    }

  // start adding it

  if (CheckAllocation(item, 4) == 0)
      return 0;  // 4 bytes used for the id.

  int32 = (int) htonl((unsigned int)id);
  p = ((unsigned char *) item->pbuf) + item->used_size;
  *((int *)p) = int32;
  item->used_size += 4;

  if (AddValue(item, type, pvalue, size) == 0)
  {
    item->used_size -= 4;
    return 0;
  }

  item->count++;

  return 1;

}

/***************************************************************************/

BINN_PRIVATE void * compress_int(int *pstorage_type, int *ptype, void *psource)
{
    int storage_type;
    int storage_type2;
    int type;
    int type2=0;
  int64  vint = 0;
  uint64 vuint;
  char *pvalue;
#if __BYTE_ORDER == __BIG_ENDIAN
  int size1, size2;
#endif

  storage_type = *pstorage_type;
    
  if (storage_type == BINN_STORAGE_BYTE)
      return psource;

    type = *ptype;

    switch (type)
    {
      case BINN_INT64:
        vint = *(int64*)psource;
//#warning goto use
        goto loc_signed;
//#warning goto use
      case BINN_INT32:
        vint = *(int*)psource;
        goto loc_signed;
//#warning goto use
      case BINN_INT16:
        vint = *(short*)psource;
        goto loc_signed;
//#warning goto use
      case BINN_UINT64:
        vuint = *(uint64*)psource;
        goto loc_positive;
//#warning goto use
      case BINN_UINT32:
        vuint = *(unsigned int*)psource;
        goto loc_positive;
//#warning goto use
      case BINN_UINT16:
        vuint = *(unsigned short*)psource;
        goto loc_positive;
//#warning goto use
  }

loc_signed:

  if (vint >= 0)
  {
    vuint = (uint64)vint;
//#warning goto use
    goto loc_positive;
  }

//loc_negative:

  if (vint >= INT8_MIN)
  {
    type2 = BINN_INT8;
  }
  else
  if (vint >= INT16_MIN)
  {
    type2 = BINN_INT16;
  }
  else
  if (vint >= INT32_MIN)
  {
    type2 = BINN_INT32;
  }
    
//#warning goto use
  goto loc_exit;

loc_positive:

  if (vuint <= UINT8_MAX) {
    type2 = BINN_UINT8;
  } else
  if (vuint <= UINT16_MAX) {
    type2 = BINN_UINT16;
  } else
  if (vuint <= UINT32_MAX) {
    type2 = BINN_UINT32;
  }

loc_exit:

  pvalue = (char *) psource;

  if ((type2) && (type2 != type))
  {
    *ptype = type2;
    storage_type2 = binn_get_write_storage(type2);
    *pstorage_type = storage_type2;
#if __BYTE_ORDER == __BIG_ENDIAN
    size1 = get_storage_size(storage_type);
    size2 = get_storage_size(storage_type2);
    pvalue += (size1 - size2);
#endif
  }

  return pvalue;

}

/***************************************************************************/



BINN_PRIVATE uint8_t AddValue(binn *item, int type, void *pvalue, int size)
{
    DEBUG_ASSERT(pvalue);
    
  int   int32, ArgSize, storage_type, extra_type;
  short int16;
  uint64 int64;
  unsigned char *p, *ptr;

  binn_get_type_info(type, &storage_type, &extra_type);

  if (pvalue == NULL)
  {
    switch (storage_type)
      {
      case BINN_STORAGE_NOBYTES:
        break;
      case BINN_STORAGE_BLOB:
      case BINN_STORAGE_STRING:
        if (size == 0) break; // the 2 above are allowed to have 0 length
      default:
        return 0;
    }
  }

  if ((type_family(type) == BINN_FAMILY_INT) && (item->disable_int_compression == 0))
  {
    pvalue = compress_int(&storage_type, &type, pvalue);
  }
    
  switch (storage_type)
    {
        case BINN_STORAGE_NOBYTES:
            size = 0;
            ArgSize = size;
            break;
        case BINN_STORAGE_BYTE:
            size = 1;
            ArgSize = size;
            break;
        case BINN_STORAGE_WORD:
            size = 2;
            ArgSize = size;
            break;
        case BINN_STORAGE_DWORD:
            size = 4;
            ArgSize = size;
            break;
        case BINN_STORAGE_QWORD:
            size = 8;
            ArgSize = size;
            break;
        case BINN_STORAGE_BLOB:
            if (size < 0)
                return 0;
            
            //if (size == 0) ...
            ArgSize = size + 4;
            break;
    case BINN_STORAGE_STRING:
            if (size < 0)
                return 0;
            if (size == 0)
                size = (int)strlen2( (char *) pvalue);
            ArgSize = size + 5; // at least this size
            
            break;
    
        case BINN_STORAGE_CONTAINER:
            if (size <= 0)
                return 0;
            
            ArgSize = size;
            
            break;
        default:
            return 0;
  }

  ArgSize += 2;  // at least 2 bytes used for data_type.
    
  if (CheckAllocation(item, ArgSize) == 0)
      return 0;

  // Gets the pointer to the next place in buffer
  p = ((unsigned char *) item->pbuf) + item->used_size;

  // If the data is not a container, store the data type
  if (storage_type != BINN_STORAGE_CONTAINER)
  {
    ptr = (unsigned char *) &type;
    if (type > 255)
    {
      type = htons((unsigned short) type);  // correct the endianess, if needed
      *p = *ptr; p++;
      item->used_size++;
      ptr++;
    }
    *p = *ptr; p++;
    item->used_size++;
  }

    switch (storage_type)
    {
        case BINN_STORAGE_NOBYTES:
          // Nothing to do.
          break;
        case BINN_STORAGE_BYTE:
          //*((char *) p) = (char) Value;
          *((char *) p) = *((char *) pvalue);
          item->used_size += 1;
          break;
        case BINN_STORAGE_WORD:
          //int16 = htons( (short) Value);
          int16 = *((short *) pvalue);
          int16 = (short)htons((unsigned short)int16);
          *((short *) p) = int16;
          item->used_size += 2;
          break;
        case BINN_STORAGE_DWORD:
          //int32 = htonl( (int) Value);
          int32 = *((int *) pvalue);
          int32 = (int)htonl((unsigned int)int32);
          *((int *) p) = int32;
          item->used_size += 4;
          break;
        case BINN_STORAGE_QWORD:
          // is there an htond or htonq to be used with qwords? (64 bits)
          int64 = *((uint64 *) pvalue);
          int64 = htonll(int64);
          *((uint64 *) p) = int64;
          item->used_size += 8;
          break;
        case BINN_STORAGE_BLOB:
          int32 = (int)htonl((unsigned int) size);
          *((int *) p) = int32;
          p += 4;
          memcpy(p, pvalue, size);
          item->used_size += 4 + size;
          break;
        case BINN_STORAGE_STRING:
          if (size > 127)
          {
            int32 = size | (int)0x80000000 ;
            int32 = (int) htonl((unsigned int) int32);
            *((int *) p) = int32;
            p += 4;
            item->used_size += 4;
          }
          else
          {
            *((unsigned char *) p) = (unsigned char) size;
            p++;
            item->used_size++;
          }
        
              if( pvalue == NULL)
              {
                  DEBUG_ASSERT(0);
                  return 0;
              }
              
          memcpy(p, pvalue, size);
          p += size;
          *((char *) p) = (char) 0;
          size++;  // null terminator
          item->used_size += size;
          break;
            
        case BINN_STORAGE_CONTAINER:
          memcpy(p, pvalue, size);
          item->used_size += size;
          break;
  }

  item->dirty = 1;

  return 1;
}

/***************************************************************************/

BINN_PRIVATE uint8_t binn_save_header(binn *item)
{
  unsigned char byte, *p;
  int int32, size;

  if (item == NULL) return 0;

#ifndef BINN_DISABLE_SMALL_HEADER

  p = ((unsigned char *) item->pbuf) + MAX_BINN_HEADER;
  size = item->used_size - MAX_BINN_HEADER + 3;  // at least 3 bytes for the header

  // write the count
  if (item->count > 127) {
    p -= 4;
    size += 3;
    int32 = item->count |(int) 0x80000000;
    int32 = (int)htonl((unsigned int)int32);
    *((int *)p) = int32;
  } else {
    p--;
    *p = (unsigned char) item->count;
  }

  // write the size
  if (size > 127) {
    p -= 4;
    size += 3;
    int32 = size |(int) 0x80000000;
    int32 =(int) htonl(( unsigned int)int32);
    *((int *)p) = int32;
  } else {
    p--;
    *p = (unsigned char) size;
  }

  // write the type.
  p--;
  *p = (unsigned char) item->type;

  // set the values
  item->ptr = p;
  item->size = size;

  UNUSED(byte);

#else

  p = (unsigned char *) item->pbuf;

  // write the type.
  byte = item->type;
  *p = byte; p++;
  // write the size
  int32 = item->used_size | 0x80000000;
  int32 = htonl(int32);
  *((int *)p) = int32; p+=4;
  // write the count
  int32 = item->count | 0x80000000;
  int32 = htonl(int32);
  *((int *)p) = int32;

  item->ptr = item->pbuf;
  item->size = item->used_size;

#endif

  item->dirty = 0;

  return 1;

}

/***************************************************************************/

void APIENTRY binn_free(binn *item) {

  if (item == NULL) return;

  if ((item->writable) && (item->pre_allocated == 0))
  {
    free_fn(item->pbuf);
  }

  if (item->freefn) item->freefn(item->ptr);

  if (item->allocated) {
    free_fn(item);
  } else {
    memset(item, 0, sizeof(binn));
    item->header = BINN_MAGIC;
  }

}

/***************************************************************************/
// free the binn structure but keeps the binn buffer allocated, returning a pointer to it. use the free function to release the buffer later
void * APIENTRY binn_release(binn *item) {
  void *data;

  if (item == NULL) return NULL;

  data = binn_ptr(item);

  if (data > item->pbuf) {
    memmove(item->pbuf, data, item->size);
    data = item->pbuf;
  }

  if (item->allocated) {
    free_fn(item);
  } else {
    memset(item, 0, sizeof(binn));
    item->header = BINN_MAGIC;
  }

  return data;

}

/***************************************************************************/

BINN_PRIVATE uint8_t IsValidBinnHeader( const void *pbuf, int *ptype, int *pcount, int *psize, int *pheadersize)
{
  unsigned char byte, *p;
  int int32, type, size, count;

  if (pbuf == NULL) return 0;

  // get the type
  p = (unsigned char *) pbuf;
  byte = *p; p++;
  if ((byte & BINN_STORAGE_MASK) != BINN_STORAGE_CONTAINER)
      return 0;
    
  if (byte & BINN_STORAGE_HAS_MORE)
      return 0;
    
  type = byte;

  switch (type) {
    case BINN_LIST:
    case BINN_MAP:
    case BINN_OBJECT:
      break;
    default:
      return 0;
  }

  // get the size
  int32 = *((unsigned char*)p);
  if (int32 & 0x80) {
    int32 = *((int*)p); p+=4;
    int32 =  (int)ntohl( (unsigned int) int32);
    int32 &= 0x7FFFFFFF;
  } else {
    p++;
  }
  size = int32;

  // get the count
  int32 = *((unsigned char*)p);
  if (int32 & 0x80) {
    int32 = *((int*)p); p+=4;
    int32 = (int) ntohl( (unsigned int)int32);
    int32 &= 0x7FFFFFFF;
  } else {
    p++;
  }
  count = int32;

#if 0
  // get the size
  int32 = *((int *)p); p+=4;
  size = ntohl(int32);
  size &= 0x7FFFFFFF;

  // get the count
  int32 = *((int *)p); p+=4;
  count = ntohl(int32);
  count &= 0x7FFFFFFF;
#endif

  if ((size < MIN_BINN_SIZE) || (count < 0)) return 0;

  // return the values
  if (ptype)
      *ptype  = type;
    
  if (pcount)
      *pcount = count;
    
  if (psize)
      *psize  = size;
    
  if (pheadersize) *pheadersize = (int) (p - (unsigned char*)pbuf);
    
  return 1;
}

/***************************************************************************/

BINN_PRIVATE int binn_buf_type( const void *pbuf) {
  int  type;

  if (!IsValidBinnHeader( pbuf, &type, NULL, NULL, NULL)) return INVALID_BINN;

  return type;

}

/***************************************************************************/

BINN_PRIVATE int binn_buf_count(void *pbuf) {
  int  nitems;

  if (!IsValidBinnHeader(pbuf, NULL, &nitems, NULL, NULL)) return 0;

  return nitems;

}

/***************************************************************************/

BINN_PRIVATE int binn_buf_size(void *pbuf) {
  int  size;

  if (!IsValidBinnHeader(pbuf, NULL, NULL, &size, NULL)) return 0;

  return size;

}

/***************************************************************************/

void * APIENTRY binn_ptr(void *ptr)
{
  binn *item;

  switch (binn_get_ptr_type(ptr))
    {
      case BINN_STRUCT:
        item = (binn*) ptr;
        if (item->writable && item->dirty)
        {
          binn_save_header(item);
        }
        return item->ptr;
      case BINN_BUFFER:
        return ptr;
      default:
        return NULL;
  }

}

/***************************************************************************/

int APIENTRY binn_size(void *ptr)
{
  binn *item;

  switch (binn_get_ptr_type(ptr))
    {
        case BINN_STRUCT:
            item = (binn*) ptr;
            if (item->writable && item->dirty)
            {
                binn_save_header(item);
            }
            return item->size;
        case BINN_BUFFER:
            return binn_buf_size(ptr);
        default:
            return 0;
  }

}

/***************************************************************************/

int APIENTRY binn_type(const void *ptr)
{
    binn *item;

    switch (binn_get_ptr_type(ptr))
    {
        case BINN_STRUCT:
            item = (binn*) ptr;
            return item->type;
        case BINN_BUFFER:
            return binn_buf_type(ptr);
        default:
            return -1;
    }

}

/***************************************************************************/

int APIENTRY binn_count(void *ptr)
{
    binn *item;

    switch (binn_get_ptr_type(ptr))
    {
        case BINN_STRUCT:
            item = (binn*) ptr;
            return item->count;
        case BINN_BUFFER:
            return binn_buf_count(ptr);
        default:
            return -1;
  }

}

/***************************************************************************/

uint8_t APIENTRY binn_is_valid(void *ptr, int *ptype, int *pcount, int *psize)
{
    int  i, type, count, size, header_size;
    unsigned char *p, *plimit, len;
    void *pbuf;
    
    uint8_t valid = 1;

    pbuf = binn_ptr(ptr);
    
    if (pbuf == NULL)
        return 0;

    if (!IsValidBinnHeader(pbuf, &type, &count, &size, &header_size))
        return 0;

  // it could compare the content size with the size informed on the header

  p = (unsigned char *)pbuf;
  plimit = p + size;

  p += header_size;

  // process all the arguments.
  for (i = 0; i < count; i++)
  {
      switch (type)
      {
          case BINN_OBJECT:
              // gets the string size (argument name)
              len = *p;
              p++;
              //if (len == 0) goto Invalid;
              // increment the used space
              p += len;
              break;
              
          case BINN_MAP:
              // increment the used space
              p += 4;
              break;
       //case BINN_LIST:
      //  break;
      }
      // xxx
      p = AdvanceDataPos(p);
      if ((p == 0) > (p > plimit))
      {
          valid = 0;
          break;
          /*
        #warning goto use
        goto Invalid;
           */
      }
  }

    if( valid == 0)
        return 0;

  if (ptype)
      *ptype  = type;
    
  if (pcount)
      *pcount = count;
  if (psize)
      *psize  = size;
    
  return 1;

/*Invalid:
  return 0;
*/
}

/***************************************************************************/
/*** INTERNAL FUNCTIONS ****************************************************/
/***************************************************************************/

BINN_PRIVATE uint8_t GetValue(unsigned char *p, binn *value)
{
  unsigned char byte;
  int   data_type, storage_type;  //, extra_type;
  int   DataSize;
  void *p2;

  if (value == NULL) return 0;
  memset(value, 0, sizeof(binn));
  value->header = BINN_MAGIC;
  //value->allocated = 0;  --  already zeroed
  //value->writable = 0;

  // saves for use with BINN_STORAGE_CONTAINER
  p2 = p;

  // read the data type
  byte = *p; p++;
  storage_type = byte & BINN_STORAGE_MASK;
  if (byte & BINN_STORAGE_HAS_MORE) {
    data_type = byte << 8;
    byte = *p; p++;
    data_type |= byte;
    //extra_type = data_type & BINN_TYPE_MASK16;
  } else {
    data_type = byte;
    //extra_type = byte & BINN_TYPE_MASK;
  }

  //value->storage_type = storage_type;
  value->type = data_type;

  switch (storage_type) {
  case BINN_STORAGE_NOBYTES:
    break;
  case BINN_STORAGE_BYTE:
    value->value.vuint8 = *((unsigned char *) p);
    value->ptr = p;   //value->ptr = &value->vuint8;
    break;
  case BINN_STORAGE_WORD:
    value->value.vint16 = *((short *) p);
    value->value.vint16 = (signed short) ntohs((unsigned short )value->value.vint16);
    value->ptr = &value->value.vint16;
    break;
  case BINN_STORAGE_DWORD:
    value->value.vint32 = *((int *) p);
    value->value.vint32 =(int) ntohl((unsigned int )value->value.vint32);
    value->ptr = &value->value.vint32;
    break;
  case BINN_STORAGE_QWORD:
    value->value.vint64 =(int64) *((uint64 *) p);
    value->value.vint64 = (int64) ntohll(value->value.vint64);
    value->ptr = &value->value.vint64;
    break;
  case BINN_STORAGE_BLOB:
    value->size = *((int*)p); p+=4;
    value->size =  (int)ntohl( (unsigned int) value->size);
    value->ptr = p;
    break;
  case BINN_STORAGE_CONTAINER:
    value->ptr = p2;  // <-- it returns the pointer to the container, not the data
    if (IsValidBinnHeader(p2, NULL, &value->count, &value->size, NULL) == 0) return 0;
    break;
  case BINN_STORAGE_STRING:
    DataSize = *((unsigned char*)p);
    if (DataSize & 0x80) {
      DataSize = *((int*)p); p+=4;
      DataSize = (int)ntohl((unsigned int)DataSize);
      DataSize &= 0x7FFFFFFF;
    } else {
      p++;
    }
    value->size = DataSize;
    value->ptr = p;
    break;
  default:
    return 0;
  }

  // convert the returned value, if needed

  switch (value->type)
    {

#ifdef BINN_EXTENDED
    case BINN_SINGLE_STR:
      value->type = BINN_SINGLE;
      value->vfloat = (float) atof((const char*)value->ptr);  // converts from string to double, and then to float
      value->ptr = &value->vfloat;
      break;
    case BINN_DOUBLE_STR:
      value->type = BINN_DOUBLE;
      value->vdouble = atof((const char*)value->ptr);  // converts from string to double
      value->ptr = &value->vdouble;
      break;
#endif
    /*
    case BINN_DECIMAL:
    case BINN_CURRENCYSTR:
    case BINN_DATE:
    case BINN_DATETIME:
    case BINN_TIME:
    */
  }

  return 1;

}

/***************************************************************************/

#if __BYTE_ORDER == __LITTLE_ENDIAN

// on little-endian devices we store the value so we can return a pointer to integers.
// it's valid only for single-threaded apps. multi-threaded apps must use the _get_ functions instead.

binn local_value;

BINN_PRIVATE void * store_value(binn *value) {

  memcpy(&local_value, value, sizeof(binn));

  switch (binn_get_read_storage(value->type)) {
  case BINN_STORAGE_NOBYTES:
    // return a valid pointer
  case BINN_STORAGE_WORD:
  case BINN_STORAGE_DWORD:
  case BINN_STORAGE_QWORD:
    return &local_value.value.vint32;  // returns the pointer to the converted value, from big-endian to little-endian
  }

  return value->ptr;   // returns from the on stack value to be thread-safe (for list, map, object, string and blob)

}

#endif

/***************************************************************************/
/*** READ FUNCTIONS ********************************************************/
/***************************************************************************/

uint8_t APIENTRY binn_object_get_value(void *ptr, const char *key, binn *value)
{
  int type, count, size, header_size;
  unsigned char *p;

  ptr = binn_ptr(ptr);
  if ((ptr == 0) || (key == 0) || (value == 0))
      return 0;

  // check the header
  if (IsValidBinnHeader(ptr, &type, &count, &size, &header_size) == 0) return 0;

  if (type != BINN_OBJECT) return 0;
  if (count == 0) return 0;

  p = (unsigned char *) ptr;
  p = SearchForKey(p, header_size, size, count, key);
  if (p == 0) return 0;

  return GetValue(p, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_map_get_value(void* ptr, int id, binn *value)
{
  int type, count, size, header_size;
  unsigned char *p;

  ptr = binn_ptr(ptr);
  if ((ptr == 0) || (value == 0)) return 0;

  // check the header
  if (IsValidBinnHeader(ptr, &type, &count, &size, &header_size) == 0) return 0;

  if (type != BINN_MAP) return 0;
  if (count == 0) return 0;

  p = (unsigned char *) ptr;
  p = SearchForID(p, header_size, size, count, id);
    
  if (p == 0)
      return 0;

  return GetValue(p, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_list_get_value(void* ptr, int pos, binn *value) {
  int  i, type, count, size, header_size;
  unsigned char *p, *plimit;

  ptr = binn_ptr(ptr);
  if ((ptr == 0) || (value == 0)) return 0;

  // check the header
  if (IsValidBinnHeader(ptr, &type, &count, &size, &header_size) == 0 ) return 0;

  if (type != BINN_LIST) return 0;
  if (count == 0) return 0;
  if ((pos <= 0) | (pos > count)) return 0;
  pos--;  // convert from base 1 to base 0

  p = (unsigned char *) ptr;
  plimit = p + size;
  p += header_size;

  for (i = 0; i < pos; i++) {
    p = AdvanceDataPos(p);
    if ((p == 0) || (p > plimit)) return 0;
  }

  return GetValue(p, value);

}

/***************************************************************************/
/*** READ PAIR BY POSITION *************************************************/
/***************************************************************************/


BINN_PRIVATE uint8_t binn_read_pair(int expected_type, void *ptr, int pos, int *pid, char *pkey, binn *value) {
  int  type, count, size, header_size;
  int  i, int32, id = 0, counter=0;
    unsigned char *p;
    unsigned char *plimit;
    unsigned char *key;
    unsigned char len = 0;

  ptr = binn_ptr(ptr);

  // check the header
  if (IsValidBinnHeader(ptr, &type, &count, &size, &header_size) == 0) return 0;

  if ((type != expected_type) || (count == 0) || (pos < 1) || (pos > count)) return 0;

  p = (unsigned char *) ptr;
  plimit = p + size - 1;
  p += header_size;

  for (i = 0; i < count; i++) {
    switch (type) {
      case BINN_MAP:
        int32 = *((int*)p); p += 4;
        int32 = (int) ntohl( (unsigned int)int32);
        if (p > plimit) return 0;
        id = int32;
        break;
      case BINN_OBJECT:
        len = *((unsigned char *)p); p++;
        if (p > plimit) return 0;
        key = p;
        p += len;
        if (p > plimit) return 0;
        break;
    }
    counter++;
    if (counter == pos)
    {
//#warning goto use
        goto found;
    }
    //
    p = AdvanceDataPos(p);
    if ((p == 0) || (p > plimit)) return 0;
  }

  return 0;

found:

  switch (type) {
    case BINN_MAP:
      if (pid) *pid = id;
      break;
    case BINN_OBJECT:
      if (pkey) {
        memcpy(pkey, key, len);
        pkey[len] = 0;
      }
      break;
  }

  return GetValue(p, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_map_get_pair(void *ptr, int pos, int *pid, binn *value) {

  return binn_read_pair(BINN_MAP, ptr, pos, pid, NULL, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_object_get_pair(void *ptr, int pos, char *pkey, binn *value) {

  return binn_read_pair(BINN_OBJECT, ptr, pos, NULL, pkey, value);

}

/***************************************************************************/

binn * APIENTRY binn_map_pair(void *map, int pos, int *pid) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_read_pair(BINN_MAP, map, pos, pid, NULL, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/

binn * APIENTRY binn_object_pair(void *obj, int pos, char *pkey) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_read_pair(BINN_OBJECT, obj, pos, NULL, pkey, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/
/***************************************************************************/

void * APIENTRY binn_map_read_pair(void *ptr, int pos, int *pid, int *ptype, int *psize) {
  binn value;

  if (binn_map_get_pair(ptr, pos, pid, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/

void * APIENTRY binn_object_read_pair(void *ptr, int pos, char *pkey, int *ptype, int *psize) {
  binn value;

  if (binn_object_get_pair(ptr, pos, pkey, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/
/*** SEQUENTIAL READ FUNCTIONS *********************************************/
/***************************************************************************/

uint8_t APIENTRY binn_iter_init(binn_iter *iter, void *ptr, int expected_type) {
  int  type, count, size, header_size;

  ptr = binn_ptr(ptr);
  if ((ptr == 0) || (iter == 0)) return 0;
  memset(iter, 0, sizeof(binn_iter));

  // check the header
  if (IsValidBinnHeader(ptr, &type, &count, &size, &header_size) == 0) return 0;

  if (type != expected_type) return 0;
  //if (count == 0) return 0;  -- should not be used

  iter->plimit = (unsigned char *)ptr + size - 1;
  iter->pnext = (unsigned char *)ptr + header_size;
  iter->count = count;
  iter->current = 0;
  iter->type = type;

  return 1;
}

/***************************************************************************/

uint8_t APIENTRY binn_list_next(binn_iter *iter, binn *value) {
  unsigned char *pnow;

  if ((iter == 0) || (iter->pnext == 0) || (iter->pnext > iter->plimit) || (iter->current > iter->count) || (iter->type != BINN_LIST)) return 0;

  iter->current++;
  if (iter->current > iter->count) return 0;

  pnow = iter->pnext;
  iter->pnext = AdvanceDataPos(pnow);

  return GetValue(pnow, value);

}

/***************************************************************************/


BINN_PRIVATE uint8_t binn_read_next_pair(int expected_type, binn_iter *iter, int *pid, char *pkey, binn *value) {
  int  int32, id;
  unsigned char *p, *key;
  unsigned short len;

  if ((iter == 0) || (iter->pnext == 0) || (iter->pnext > iter->plimit) || (iter->current > iter->count) || (iter->type != expected_type)) return 0;

  iter->current++;
  if (iter->current > iter->count) return 0;

  p = iter->pnext;

  switch (expected_type) {
    case BINN_MAP:
      int32 = *((int*)p); p += 4;
      int32 =(int) ntohl(( unsigned int) int32);
      if (p > iter->plimit) return 0;
      id = int32;
      if (pid) *pid = id;
      break;
    case BINN_OBJECT:
      len = *((unsigned char *)p); p++;
      key = p;
      p += len;
      if (p > iter->plimit) return 0;
      if (pkey) {
        memcpy(pkey, key, len);
        pkey[len] = 0;
      }
      break;
  }

  iter->pnext = AdvanceDataPos(p);

  return GetValue(p, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_map_next(binn_iter *iter, int *pid, binn *value) {

  return binn_read_next_pair(BINN_MAP, iter, pid, NULL, value);

}

/***************************************************************************/

uint8_t APIENTRY binn_object_next(binn_iter *iter, char *pkey, binn *value) {

  return binn_read_next_pair(BINN_OBJECT, iter, NULL, pkey, value);

}

/***************************************************************************/
/***************************************************************************/

binn * APIENTRY binn_list_next_value(binn_iter *iter)
{
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_list_next(iter, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/

binn * APIENTRY binn_map_next_value(binn_iter *iter, int *pid) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_map_next(iter, pid, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/

binn * APIENTRY binn_object_next_value(binn_iter *iter, char *pkey) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_object_next(iter, pkey, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/
/***************************************************************************/

void * APIENTRY binn_list_read_next(binn_iter *iter, int *ptype, int *psize) {
  binn value;

  if (binn_list_next(iter, &value) == 0)
      return NULL;
  if (ptype)
      *ptype = value.type;
    
  if (psize)
      *psize = value.size;
    
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/

void * APIENTRY binn_map_read_next(binn_iter *iter, int *pid, int *ptype, int *psize) {
  binn value;

  if (binn_map_next(iter, pid, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/

void * APIENTRY binn_object_read_next(binn_iter *iter, char *pkey, int *ptype, int *psize) {
  binn value;

  if (binn_object_next(iter, pkey, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/*************************************************************************************/
/****** EXTENDED INTERFACE ***********************************************************/
/****** none of the functions above call the functions below *************************/
/*************************************************************************************/

int APIENTRY binn_get_write_storage(int type) {
  int storage_type;

  switch (type) {
    case BINN_SINGLE_STR:
    case BINN_DOUBLE_STR:
      return BINN_STORAGE_STRING;

    default:
      binn_get_type_info(type, &storage_type, NULL);
      return storage_type;
  }

}

/*************************************************************************************/

int APIENTRY binn_get_read_storage(int type) {
  int storage_type;

  switch (type) {
#ifdef BINN_EXTENDED
    case BINN_SINGLE_STR:
      return BINN_STORAGE_DWORD;
    case BINN_DOUBLE_STR:
      return BINN_STORAGE_QWORD;
#endif

    default:
      binn_get_type_info(type, &storage_type, NULL);
      return storage_type;
  }

}

/*************************************************************************************/



BINN_PRIVATE uint8_t GetWriteConvertedData(int *ptype, void **ppvalue, int *psize) {
  int  type;
  float  f1;
  double d1;
  char pstr[128];

  UNUSED(pstr);
  UNUSED(d1);
  UNUSED(f1);

  type = *ptype;

  if (*ppvalue == NULL) {
    switch (type) {
      case BINN_NULL:
        break;
      case BINN_STRING:
      case BINN_BLOB:
        if (*psize == 0) break;
      default:
        return 0;
    }
  }

  switch (type) {
#ifdef BINN_EXTENDED
    case BINN_SINGLE:
      f1 = **(float**)ppvalue;
      d1 = f1;  // convert from float (32bits) to double (64bits)
      type = BINN_SINGLE_STR;
#warning goto use
      goto conv_double;
    case BINN_DOUBLE:
      d1 = **(double**)ppvalue;
      type = BINN_DOUBLE_STR;
conv_double:
      // the '%.17e' is more precise than the '%g'
      snprintf(pstr, 127, "%.17e", d1);
      *ppvalue = pstr;
      *ptype = type;
      break;
#endif
    case BINN_DECIMAL:
    case BINN_CURRENCYSTR:
      /*
      if (binn_malloc_extptr(128) == NULL) return 0;
      snprintf(sptr, 127, "%E", **ppvalue);
      *ppvalue = sptr;
      */
      return 1;  //! temporary
      break;

    case BINN_DATE:
    case BINN_DATETIME:
    case BINN_TIME:
      return 1;  //! temporary
      break;


  }

  return 1;

}

/*************************************************************************************/

BINN_PRIVATE int type_family(int type)  {

  switch (type) {
    case BINN_LIST:
    case BINN_MAP:
    case BINN_OBJECT:
      return BINN_FAMILY_BINN;

    case BINN_INT8:
    case BINN_INT16:
    case BINN_INT32:
    case BINN_INT64:
    case BINN_UINT8:
    case BINN_UINT16:
    case BINN_UINT32:
    case BINN_UINT64:
      return BINN_FAMILY_INT;

    case BINN_FLOAT32:
    case BINN_FLOAT64:
    //case BINN_SINGLE:
    case BINN_SINGLE_STR:
    //case BINN_DOUBLE:
    case BINN_DOUBLE_STR:
      return BINN_FAMILY_FLOAT;

    case BINN_STRING:
    case BINN_HTML:
    case BINN_CSS:
    case BINN_XML:
    case BINN_JSON:
    case BINN_JAVASCRIPT:
      return BINN_FAMILY_STRING;

    case BINN_BLOB:
    case BINN_JPEG:
    case BINN_GIF:
    case BINN_PNG:
    case BINN_BMP:
      return BINN_FAMILY_BLOB;

    case BINN_DECIMAL:
    case BINN_CURRENCY:
    case BINN_DATE:
    case BINN_TIME:
    case BINN_DATETIME:
      return BINN_FAMILY_STRING;

    case BINN_NULL:
      return BINN_FAMILY_NULL;

    default:
      // if it wasn't found
      return BINN_FAMILY_NONE;
  }

}

/*************************************************************************************/

BINN_PRIVATE int int_type(int type)  {

  switch (type) {
  case BINN_INT8:
  case BINN_INT16:
  case BINN_INT32:
  case BINN_INT64:
    return BINN_SIGNED_INT;

  case BINN_UINT8:
  case BINN_UINT16:
  case BINN_UINT32:
  case BINN_UINT64:
    return BINN_UNSIGNED_INT;

  default:
    return 0;
  }

}

/*************************************************************************************/

BINN_PRIVATE uint8_t copy_raw_value(void *psource, void *pdest, int data_store) {

  switch (data_store) {
  case BINN_STORAGE_NOBYTES:
    break;
  case BINN_STORAGE_BYTE:
    *((char *) pdest) = *(char *)psource;
    break;
  case BINN_STORAGE_WORD:
    *((short *) pdest) = *(short *)psource;
    break;
  case BINN_STORAGE_DWORD:
    *((int *) pdest) = *(int *)psource;
    break;
  case BINN_STORAGE_QWORD:
    *((uint64 *) pdest) = *(uint64 *)psource;
    break;
  case BINN_STORAGE_BLOB:
  case BINN_STORAGE_STRING:
  case BINN_STORAGE_CONTAINER:
    *((char **) pdest) = (char *)psource;
    break;
  default:
    return 0;
  }

  return 1;

}

/*************************************************************************************/

BINN_PRIVATE uint8_t copy_int_value(void *psource, void *pdest, int source_type, int dest_type) {
  uint64 vuint64 = 0;
  int64 vint64 = 0;

  switch (source_type) {
  case BINN_INT8:
    vint64 = *(signed char *)psource;
    break;
  case BINN_INT16:
    vint64 = *(short *)psource;
    break;
  case BINN_INT32:
    vint64 = *(int *)psource;
    break;
  case BINN_INT64:
    vint64 = *(int64 *)psource;
    break;

  case BINN_UINT8:
    vuint64 = *(unsigned char *)psource;
    break;
  case BINN_UINT16:
    vuint64 = *(unsigned short *)psource;
    break;
  case BINN_UINT32:
    vuint64 = *(unsigned int *)psource;
    break;
  case BINN_UINT64:
    vuint64 = *(uint64 *)psource;
    break;

  default:
    return 0;
  }


  // copy from int64 to uint64, if possible

  if ((int_type(source_type) == BINN_UNSIGNED_INT) && (int_type(dest_type) == BINN_SIGNED_INT))
  {
    if (vuint64 > INT64_MAX)
        return 0;
    
      vint64 = (int64)vuint64;
  }
  else if ((int_type(source_type) == BINN_SIGNED_INT) && (int_type(dest_type) == BINN_UNSIGNED_INT))
  {
    if (vint64 < 0)
        return 0;
      
    vuint64 =(uint64) vint64;
  }


  switch (dest_type) {
  case BINN_INT8:
    if ((vint64 < INT8_MIN) || (vint64 > INT8_MAX)) return 0;
    *(signed char *)pdest = (signed char) vint64;
    break;
  case BINN_INT16:
    if ((vint64 < INT16_MIN) || (vint64 > INT16_MAX)) return 0;
    *(short *)pdest = (short) vint64;
    break;
  case BINN_INT32:
    if ((vint64 < INT32_MIN) || (vint64 > INT32_MAX)) return 0;
    *(int *)pdest = (int) vint64;
    break;
  case BINN_INT64:
    *(int64 *)pdest = vint64;
    break;

  case BINN_UINT8:
    if (vuint64 > UINT8_MAX) return 0;
    *(unsigned char *)pdest = (unsigned char) vuint64;
    break;
  case BINN_UINT16:
    if (vuint64 > UINT16_MAX) return 0;
    *(unsigned short *)pdest = (unsigned short) vuint64;
    break;
  case BINN_UINT32:
    if (vuint64 > UINT32_MAX) return 0;
    *(unsigned int *)pdest = (unsigned int) vuint64;
    break;
  case BINN_UINT64:
    *(uint64 *)pdest = vuint64;
    break;

  default:
    return 0;
  }

  return 1;

}

/*************************************************************************************/

BINN_PRIVATE uint8_t copy_float_value(void *psource, void *pdest, int source_type, int dest_type) {

    UNUSED(dest_type);
    
    DEBUG_ASSERT(psource);
    DEBUG_ASSERT(pdest);
    
    switch (source_type)
    {
        case BINN_FLOAT32:
            *(double *)pdest = *(float *)psource;
            break;
        case BINN_FLOAT64:
            *(float *)pdest = (float) *(double *)psource;
            break;
        default:
            return 0;
  }

  return 1;

}

/*************************************************************************************/

BINN_PRIVATE void zero_value(void *pvalue, int type)
{
  //int size=0;

    DEBUG_ASSERT(pvalue);
    switch (binn_get_read_storage(type))
    {
        case BINN_STORAGE_NOBYTES:
            break;
        case BINN_STORAGE_BYTE:
            *((char *) pvalue) = 0;
            //size=1;
            break;
        case BINN_STORAGE_WORD:
            *((short *) pvalue) = 0;
            //size=2;
            break;
        case BINN_STORAGE_DWORD:
            *((int *) pvalue) = 0;
            //size=4;
            break;
        case BINN_STORAGE_QWORD:
            *((uint64 *) pvalue) = 0;
            //size=8;
            break;
        case BINN_STORAGE_BLOB:
        case BINN_STORAGE_STRING:
        case BINN_STORAGE_CONTAINER:
            *(char **)pvalue = NULL;
            break;
  }

  //if (size>0) memset(pvalue, 0, size);

}

/*************************************************************************************/

BINN_PRIVATE uint8_t copy_value(void *psource, void *pdest, int source_type, int dest_type, int data_store) {

  if (type_family(source_type) != type_family(dest_type)) return 0;

  if ((type_family(source_type) == BINN_FAMILY_INT) && (source_type != dest_type)) {
    return copy_int_value(psource, pdest, source_type, dest_type);
  } else if ((type_family(source_type) == BINN_FAMILY_FLOAT) && (source_type != dest_type)) {
    return copy_float_value(psource, pdest, source_type, dest_type);
  } else {
    return copy_raw_value(psource, pdest, data_store);
  }

}

/*************************************************************************************/
/*** WRITE FUNCTIONS *****************************************************************/
/*************************************************************************************/

uint8_t APIENTRY binn_list_add(binn *list, int type, void *pvalue, int size) {

  if (GetWriteConvertedData(&type, &pvalue, &size) == 0) return 0;

  return binn_list_add_raw(list, type, pvalue, size);

}

/*************************************************************************************/

uint8_t APIENTRY binn_map_set(binn *map, int id, int type, void *pvalue, int size) {

  if (GetWriteConvertedData(&type, &pvalue, &size) == 0) return 0;

  return binn_map_set_raw(map, id, type, pvalue, size);

}

/*************************************************************************************/

uint8_t APIENTRY binn_object_set(binn *obj, char *key, int type, void *pvalue, int size) {

  if (GetWriteConvertedData(&type, &pvalue, &size) == 0) return 0;

  return binn_object_set_raw(obj, key, type, pvalue, size);

}

/*************************************************************************************/

// this function is used by the wrappers
uint8_t APIENTRY binn_add_value(binn *item, int binn_type, int id, char *name, int type, void *pvalue, int size) {

  switch (binn_type) {
    case BINN_LIST:
      return binn_list_add(item, type, pvalue, size);
    case BINN_MAP:
      return binn_map_set(item, id, type, pvalue, size);
    case BINN_OBJECT:
      return binn_object_set(item, name, type, pvalue, size);
    default:
      return 0;
  }

}

/*************************************************************************************/
/*************************************************************************************/

uint8_t APIENTRY binn_list_add_new(binn *list, binn *value) {
  uint8_t retval;

  retval = binn_list_add_value(list, value);
  if (value) free_fn(value);
  return retval;

}

/*************************************************************************************/

uint8_t APIENTRY binn_map_set_new(binn *map, int id, binn *value) {
  uint8_t retval;

  retval = binn_map_set_value(map, id, value);
  if (value) free_fn(value);
  return retval;

}

/*************************************************************************************/

uint8_t APIENTRY binn_object_set_new(binn *obj, char *key, binn *value) {
  uint8_t retval;

  retval = binn_object_set_value(obj, key, value);
  if (value) free_fn(value);
  return retval;

}

/*************************************************************************************/
/*** READ FUNCTIONS ******************************************************************/
/*************************************************************************************/

binn * APIENTRY binn_list_value(void *ptr, int pos) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_list_get_value(ptr, pos, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/*************************************************************************************/

binn * APIENTRY binn_map_value(void *ptr, int id) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_map_get_value(ptr, id, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/*************************************************************************************/

binn * APIENTRY binn_object_value(void *ptr, char *key) {
  binn *value;

  value = (binn *) binn_malloc(sizeof(binn));

  if (binn_object_get_value(ptr, key, value) == 0) {
    free_fn(value);
    return NULL;
  }

  value->allocated = 1;
  return value;

}

/***************************************************************************/
/***************************************************************************/

void * APIENTRY binn_list_read(void *list, int pos, int *ptype, int *psize) {
  binn value;

  if (binn_list_get_value(list, pos, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/

void * APIENTRY binn_map_read(void *map, int id, int *ptype, int *psize) {
  binn value;

  if (binn_map_get_value(map, id, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/

void * APIENTRY binn_object_read(void *obj, char *key, int *ptype, int *psize) {
  binn value;

  if (binn_object_get_value(obj, key, &value) == 0) return NULL;
  if (ptype) *ptype = value.type;
  if (psize) *psize = value.size;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  return store_value(&value);
#else
  return value.ptr;
#endif

}

/***************************************************************************/
/***************************************************************************/

uint8_t APIENTRY binn_list_get(void *ptr, int pos, int type, void *pvalue, int *psize) {
  binn value;
  int storage_type;

  storage_type = binn_get_read_storage(type);
  if ((storage_type != BINN_STORAGE_NOBYTES) && (pvalue == NULL)) return 0;

  zero_value(pvalue, type);

  if (binn_list_get_value(ptr, pos, &value) == 0) return 0;

  if (copy_value(value.ptr, pvalue, value.type, type, storage_type) == 0) return 0;

  if (psize) *psize = value.size;

  return 1;

}

/***************************************************************************/

uint8_t APIENTRY binn_map_get(void *ptr, int id, int type, void *pvalue, int *psize) {
  binn value;
  int storage_type;

  storage_type = binn_get_read_storage(type);
  if ((storage_type != BINN_STORAGE_NOBYTES) && (pvalue == NULL)) return 0;

  zero_value(pvalue, type);

  if (binn_map_get_value(ptr, id, &value) == 0) return 0;

  if (copy_value(value.ptr, pvalue, value.type, type, storage_type) == 0) return 0;

  if (psize) *psize = value.size;

  return 1;

}

/***************************************************************************/

//   if (binn_object_get(obj, "multiplier", BINN_INT32, &multiplier, NULL) == 0) xxx;

uint8_t APIENTRY binn_object_get(void *ptr, char *key, int type, void *pvalue, int *psize) {
  binn value;
  int storage_type;

  storage_type = binn_get_read_storage(type);
  if ((storage_type != BINN_STORAGE_NOBYTES) && (pvalue == NULL)) return 0;

  zero_value(pvalue, type);

  if (binn_object_get_value(ptr, key, &value) == 0) return 0;

  if (copy_value(value.ptr, pvalue, value.type, type, storage_type) == 0) return 0;

  if (psize) *psize = value.size;

  return 1;

}

/***************************************************************************/
/***************************************************************************/

// these functions below may not be implemented as inline functions, because
// they use a lot of space, even for the variable. so they will be exported.

// but what about using as static?
//    is there any problem with wrappers? can these wrappers implement these functions using the header?
//    if as static, will they be present even on modules that don't use the functions?

signed char APIENTRY binn_list_int8(void *list, int pos) {
  signed char value;

  binn_list_get(list, pos, BINN_INT8, &value, NULL);

  return value;
}

short APIENTRY binn_list_int16(void *list, int pos) {
  short value;

  binn_list_get(list, pos, BINN_INT16, &value, NULL);

  return value;
}

int APIENTRY binn_list_int32(void *list, int pos) {
  int value;

  binn_list_get(list, pos, BINN_INT32, &value, NULL);

  return value;
}

int64 APIENTRY binn_list_int64(void *list, int pos) {
  int64 value;

  binn_list_get(list, pos, BINN_INT64, &value, NULL);

  return value;
}

unsigned char APIENTRY binn_list_uint8(void *list, int pos) {
  unsigned char value;

  binn_list_get(list, pos, BINN_UINT8, &value, NULL);

  return value;
}

unsigned short APIENTRY binn_list_uint16(void *list, int pos) {
  unsigned short value;

  binn_list_get(list, pos, BINN_UINT16, &value, NULL);

  return value;
}

unsigned int APIENTRY binn_list_uint32(void *list, int pos) {
  unsigned int value;

  binn_list_get(list, pos, BINN_UINT32, &value, NULL);

  return value;
}

uint64 APIENTRY binn_list_uint64(void *list, int pos) {
  uint64 value;

  binn_list_get(list, pos, BINN_UINT64, &value, NULL);

  return value;
}

float APIENTRY binn_list_float(void *list, int pos) {
  float value;

  binn_list_get(list, pos, BINN_FLOAT32, &value, NULL);

  return value;
}

double APIENTRY binn_list_double(void *list, int pos) {
  double value;

  binn_list_get(list, pos, BINN_FLOAT64, &value, NULL);

  return value;
}



uint8_t APIENTRY binn_list_null(void *list, int pos) {

  return binn_list_get(list, pos, BINN_NULL, NULL, NULL);

}

char * APIENTRY binn_list_str(void *list, int pos) {
  char *value;

  binn_list_get(list, pos, BINN_STRING, &value, NULL);

  return value;
}

void * APIENTRY binn_list_blob(void *list, int pos, int *psize) {
  void *value;

  binn_list_get(list, pos, BINN_BLOB, &value, psize);

  return value;
}

void * APIENTRY binn_list_list(void *list, int pos) {
  void *value;

  binn_list_get(list, pos, BINN_LIST, &value, NULL);

  return value;
}

void * APIENTRY binn_list_map(void *list, int pos) {
  void *value;

  binn_list_get(list, pos, BINN_MAP, &value, NULL);

  return value;
}

void * APIENTRY binn_list_object(void *list, int pos) {
  void *value;

  binn_list_get(list, pos, BINN_OBJECT, &value, NULL);

  return value;
}

/***************************************************************************/

signed char APIENTRY binn_map_int8(void *map, int id) {
  signed char value;

  binn_map_get(map, id, BINN_INT8, &value, NULL);

  return value;
}

short APIENTRY binn_map_int16(void *map, int id) {
  short value;

  binn_map_get(map, id, BINN_INT16, &value, NULL);

  return value;
}

int APIENTRY binn_map_int32(void *map, int id) {
  int value;

  binn_map_get(map, id, BINN_INT32, &value, NULL);

  return value;
}

int64 APIENTRY binn_map_int64(void *map, int id) {
  int64 value;

  binn_map_get(map, id, BINN_INT64, &value, NULL);

  return value;
}

unsigned char APIENTRY binn_map_uint8(void *map, int id) {
  unsigned char value;

  binn_map_get(map, id, BINN_UINT8, &value, NULL);

  return value;
}

unsigned short APIENTRY binn_map_uint16(void *map, int id) {
  unsigned short value;

  binn_map_get(map, id, BINN_UINT16, &value, NULL);

  return value;
}

unsigned int APIENTRY binn_map_uint32(void *map, int id) {
  unsigned int value = 0;

  binn_map_get(map, id, BINN_UINT32, &value, NULL);

  return value;
}

uint64 APIENTRY binn_map_uint64(void *map, int id) {
  uint64 value = 0;

  binn_map_get(map, id, BINN_UINT64, &value, NULL);

  return value;
}

float APIENTRY binn_map_float(void *map, int id) {
  float value  = 0.f;

  binn_map_get(map, id, BINN_FLOAT32, &value, NULL);

  return value;
}

double APIENTRY binn_map_double(void *map, int id) {
  double value = 0.;

  binn_map_get(map, id, BINN_FLOAT64, &value, NULL);

  return value;
}



uint8_t APIENTRY binn_map_null(void *map, int id) {

  return binn_map_get(map, id, BINN_NULL, NULL, NULL);

}

char * APIENTRY binn_map_str(void *map, int id) {
  char *value = NULL;

  binn_map_get(map, id, BINN_STRING, &value, NULL);

  return value;
}

void * APIENTRY binn_map_blob(void *map, int id, int *psize) {
  void *value = NULL;

  binn_map_get(map, id, BINN_BLOB, &value, psize);

  return value;
}

void * APIENTRY binn_map_list(void *map, int id) {
  void *value = NULL;

  binn_map_get(map, id, BINN_LIST, &value, NULL);

  return value;
}

void * APIENTRY binn_map_map(void *map, int id) {
  void *value = NULL;

  binn_map_get(map, id, BINN_MAP, &value, NULL);

  return value;
}

void * APIENTRY binn_map_object(void *map, int id) {
  void *value = NULL;

  binn_map_get(map, id, BINN_OBJECT, &value, NULL);

  return value;
}

/***************************************************************************/

signed char APIENTRY binn_object_int8(void *obj, char *key) {
  signed char value;

  binn_object_get(obj, key, BINN_INT8, &value, NULL);

  return value;
}

short APIENTRY binn_object_int16(void *obj, char *key) {
  short value;

  binn_object_get(obj, key, BINN_INT16, &value, NULL);

  return value;
}

int APIENTRY binn_object_int32(void *obj, char *key) {
  int value;

  binn_object_get(obj, key, BINN_INT32, &value, NULL);

  return value;
}

int64 APIENTRY binn_object_int64(void *obj, char *key) {
  int64 value;

  binn_object_get(obj, key, BINN_INT64, &value, NULL);

  return value;
}

unsigned char APIENTRY binn_object_uint8(void *obj, char *key) {
  unsigned char value;

  binn_object_get(obj, key, BINN_UINT8, &value, NULL);

  return value;
}

unsigned short APIENTRY binn_object_uint16(void *obj, char *key) {
  unsigned short value;

  binn_object_get(obj, key, BINN_UINT16, &value, NULL);

  return value;
}

unsigned int APIENTRY binn_object_uint32(void *obj, char *key) {
  unsigned int value;

  binn_object_get(obj, key, BINN_UINT32, &value, NULL);

  return value;
}

uint64 APIENTRY binn_object_uint64(void *obj, char *key) {
  uint64 value;

  binn_object_get(obj, key, BINN_UINT64, &value, NULL);

  return value;
}

float APIENTRY binn_object_float(void *obj, char *key) {
  float value;

  binn_object_get(obj, key, BINN_FLOAT32, &value, NULL);

  return value;
}

double APIENTRY binn_object_double(void *obj, char *key) {
  double value;

  binn_object_get(obj, key, BINN_FLOAT64, &value, NULL);

  return value;
}



uint8_t APIENTRY binn_object_null(void *obj, char *key) {

  return binn_object_get(obj, key, BINN_NULL, NULL, NULL);

}

char * APIENTRY binn_object_str(void *obj, char *key) {
  char *value;

  binn_object_get(obj, key, BINN_STRING, &value, NULL);

  return value;
}

void * APIENTRY binn_object_blob(void *obj, char *key, int *psize) {
  void *value;

  binn_object_get(obj, key, BINN_BLOB, &value, psize);

  return value;
}

void * APIENTRY binn_object_list(void *obj, char *key) {
  void *value;

  binn_object_get(obj, key, BINN_LIST, &value, NULL);

  return value;
}

void * APIENTRY binn_object_map(void *obj, char *key) {
  void *value;

  binn_object_get(obj, key, BINN_MAP, &value, NULL);

  return value;
}

void * APIENTRY binn_object_object(void *obj, char *key) {
  void *value;

  binn_object_get(obj, key, BINN_OBJECT, &value, NULL);

  return value;
}

/*************************************************************************************/
/*************************************************************************************/

BINN_PRIVATE binn * binn_alloc_item() {
  binn *item;
  item = (binn *) binn_malloc(sizeof(binn));
  if (item) {
    memset(item, 0, sizeof(binn));
    item->header = BINN_MAGIC;
    item->allocated = 1;
    //item->writable = 0;  -- already zeroed
  }
  return item;
}

/*************************************************************************************/

binn * APIENTRY binn_value(int type, void *pvalue, int size, binn_mem_free freefn) {
    int storage_type;
    binn *item = binn_alloc_item();
    if (item)
    {
        item->type = type;
        binn_get_type_info(type, &storage_type, NULL);
      
        switch (storage_type)
        {
            case BINN_STORAGE_NOBYTES:
              break;
            case BINN_STORAGE_STRING:
              if (size == 0)
              {
                  size =(int) strlen((char*)pvalue) + 1;
              }
            case BINN_STORAGE_BLOB:
            case BINN_STORAGE_CONTAINER:
              if (freefn == BINN_TRANSIENT)
              {
                  item->ptr = binn_memdup(pvalue, size);
                  if (item->ptr == NULL)
                  {
                      free_fn(item);
                      return NULL;
                  }
                  item->freefn = free_fn;
                  if (storage_type == BINN_STORAGE_STRING)
                  {
                      size--;
                  }
              }
              else
              {
                  item->ptr = pvalue;
                  item->freefn = freefn;
              }
              item->size = size;
              break;
            default:
              item->ptr = &item->value.vint32;
              copy_raw_value(pvalue, item->ptr, storage_type);
        }
  }
  return item;
}

/*************************************************************************************/

uint8_t APIENTRY binn_set_string(binn *item, char *str, binn_mem_free pfree) {

  if (item == NULL || str == NULL) return 0;

  if (pfree == BINN_TRANSIENT) {
    item->ptr = binn_memdup(str, (int)(strlen(str) + 1));
    if (item->ptr == NULL) return 0;
    item->freefn = free_fn;
  } else {
    item->ptr = str;
    item->freefn = pfree;
  }

  item->type = BINN_STRING;
  return 1;

}

/*************************************************************************************/

uint8_t APIENTRY binn_set_blob(binn *item, void *ptr, int size, binn_mem_free pfree) {

  if (item == NULL || ptr == NULL) return 0;

  if (pfree == BINN_TRANSIENT) {
    item->ptr = binn_memdup(ptr, size);
    if (item->ptr == NULL) return 0;
    item->freefn = free_fn;
  } else {
    item->ptr = ptr;
    item->freefn = pfree;
  }

  item->type = BINN_BLOB;
  item->size = size;
  return 1;

}

/*************************************************************************************/
/*** READ CONVERTED VALUE ************************************************************/
/*************************************************************************************/

#ifndef _MSC_VER
int64 atoi64(char *str)
{
  int64 retval;
  int is_negative=0;

  if (*str == '-') {
    is_negative = 1;
    str++;
  }
  retval = 0;
  for (; *str; str++) {
    retval = 10 * retval + (*str - '0');
  }
  if (is_negative) retval *= -1;
  return retval;
}
#endif

/*****************************************************************************/

BINN_PRIVATE uint8_t is_integer(char *p) {
  uint8_t retval;

  if (p == NULL) return 0;
  if (*p == '-') p++;
  if (*p == 0) return 0;

  retval = 1;

  for (; *p; p++) {
    if ( (*p < '0') || (*p > '9') ) {
      retval = 0;
    }
  }

  return retval;
}

/*****************************************************************************/

BINN_PRIVATE uint8_t is_float(char *p) {
  uint8_t retval, number_found=0;

  if (p == NULL) return 0;
  if (*p == '-') p++;
  if (*p == 0) return 0;

  retval = 1;

  for (; *p; p++) {
    if ((*p == '.') || (*p == ',')) {
      if (!number_found) retval = 0;
    } else if ( (*p >= '0') && (*p <= '9') ) {
      number_found = 1;
    } else {
      return 0;
    }
  }

  return retval;
}

/*************************************************************************************/
/*
BINN_PRIVATE BOOL is_bool_str(char *str, BOOL *pbool) {
  int64  vint;
  double vdouble;

  if (str == NULL || pbool == NULL) return 0;

  if (stricmp(str, TRUE_STR) == 0)
  {
//#warning goto use
      goto loc_true;
  }
  if (stricmp(str, "yes") == 0)
  {
//#warning goto use
      goto loc_true;
  }
  if (stricmp(str, "on") == 0)
  {
//#warning goto use
      goto loc_true;
  }
  //if (stricmp(str, "1") == 0) goto loc_true;

  if (stricmp(str, 0_STR) == 0)
  {
//#warning goto use
      goto loc_false;
  }
  if (stricmp(str, "no") == 0)
  {
//#warning goto use
      goto loc_false;
  }
  if (stricmp(str, "off") == 0)
  {
//#warning goto use
      goto loc_false;
  }
  //if (stricmp(str, "0") == 0) goto loc_false;

  if (is_integer(str))
  {
    vint = atoi64(str);
    *pbool = (vint != 0) ? 1 : 0;
    return 1;
  }
  else if (is_float(str))
  {
    vdouble = atof(str);
    *pbool = (vdouble != 0) ? 1 : 0;
    return 1;
  }

  return 0;

loc_true:
  *pbool = 1;
  return TRUE;

loc_false:
  *pbool = 0;
  return TRUE;

}
*/
/*************************************************************************************/

uint8_t APIENTRY binn_get_int32(binn *value, int *pint) {

  if (value == NULL || pint == NULL) return 0;

  if (type_family(value->type) == BINN_FAMILY_INT) {
    return copy_int_value(value->ptr, pint, value->type, BINN_INT32);
  }

  switch (value->type) {
  case BINN_FLOAT:
    if ((value->value.vfloat < INT32_MIN) || (value->value.vfloat > INT32_MAX))
        return 0;
          
    *pint = (int) value->value.vfloat;
    break;
  case BINN_DOUBLE:
    if ((value->value.vdouble < INT32_MIN) || (value->value.vdouble > INT32_MAX))
        return 0;
          
    *pint = (int) value->value.vdouble;
    break;
  case BINN_STRING:
    if (is_integer((char*)value->ptr))
      *pint = atoi((char*)value->ptr);
    else if (is_float((char*)value->ptr))
      *pint =(int) atof((char*)value->ptr);
    else
      return 0;
    break;
  
  default:
    return 0;
  }

  return 1;
}

/*************************************************************************************/

uint8_t APIENTRY binn_get_int64(binn *value, int64 *pint) {

  if (value == NULL || pint == NULL) return 0;

  if (type_family(value->type) == BINN_FAMILY_INT) {
    return copy_int_value(value->ptr, pint, value->type, BINN_INT64);
  }

  switch (value->type) {
  case BINN_FLOAT:
    if ((value->value.vfloat < INT64_MIN) || (value->value.vfloat > INT64_MAX))
        return 0;
          
    *pint = (int64 ) value->value.vfloat;
    break;
  case BINN_DOUBLE:
    if ((value->value.vdouble < INT64_MIN) || (value->value.vdouble > INT64_MAX))
        return 0;
          
    *pint = (int64 ) value->value.vdouble;
    break;
  case BINN_STRING:
    if (is_integer((char*)value->ptr))
      *pint = atoi64((char*)value->ptr);
          
    else if (is_float((char*)value->ptr))
      *pint = (int64 )atof((char*)value->ptr);
          
    else
      return 0;
    break;
  
  default:
    return 0;
  }

  return 1;
}

/*************************************************************************************/

uint8_t APIENTRY binn_get_double(binn *value, double *pfloat) {
  int64 vint;

  if (value == NULL || pfloat == NULL) return 0;

  if (type_family(value->type) == BINN_FAMILY_INT) {
    if (copy_int_value(value->ptr, &vint, value->type, BINN_INT64) == 0) return 0;
    *pfloat = vint;
    return 1;
  }

  switch (value->type) {
  case BINN_FLOAT:
    *pfloat = value->value.vfloat;
    break;
  case BINN_DOUBLE:
    *pfloat = value->value.vdouble;
    break;
  case BINN_STRING:
    if (is_integer((char*)value->ptr))
      *pfloat = atoi64((char*)value->ptr);
    else if (is_float((char*)value->ptr))
      *pfloat = atof((char*)value->ptr);
    else
      return 0;
    break;
  
  default:
    return 0;
  }

  return 1;
}

/*************************************************************************************/
/*
uint8_t APIENTRY binn_get_bool(binn *value, uint8_t *pbool) {
  int64 vint;

  if (value == NULL || pbool == NULL) return 0;

  if (type_family(value->type) == BINN_FAMILY_INT) {
    if (copy_int_value(value->ptr, &vint, value->type, BINN_INT64) == 0) return 0;
    *pbool = (vint != 0) ? 1 : 0;
    return 1;
  }

  switch (value->type)
    {
      case BINN_FLOAT:
        *pbool = (value->value.vfloat != 0) ? 1 : 0;
        break;
      case BINN_DOUBLE:
        *pbool = (value->value.vdouble != 0) ? 1 : 0;
        break;
 
      //case BINN_STRING:
       // return is_bool_str((char*)value->ptr, pbool);
         
      default:
        return 0;
  }

  return 1;
}
*/
/*************************************************************************************/

char * APIENTRY binn_get_str(binn *value)
{
    int64 vint;
    char buf[128];

    if (value == NULL)
        return NULL;

    if (type_family(value->type) == BINN_FAMILY_INT)
    {
        if (copy_int_value(value->ptr, &vint, value->type, BINN_INT64) == 0)
            return NULL;
      
        sprintf(buf, "%lld", vint);
//#warning goto use
        goto loc_convert_value;
    }

    switch (value->type)
    {
        case BINN_FLOAT:
            value->value.vdouble = value->value.vfloat;
        case BINN_DOUBLE:
            sprintf(buf, "%g", value->value.vdouble);
//#warning goto use
            goto loc_convert_value;
        case BINN_STRING:
            return (char*) value->ptr;
        
  }

  return NULL;

loc_convert_value:

  //value->vint64 = 0;
  value->ptr = strdup(buf);
  if (value->ptr == NULL) return NULL;
  value->freefn = free;
  value->type = BINN_STRING;
  return (char*) value->ptr;

}

/*************************************************************************************/
/*** GENERAL FUNCTIONS ***************************************************************/
/*************************************************************************************/

uint8_t APIENTRY binn_is_container(binn *item) {

  if (item == NULL) return 0;

  switch (item->type) {
  case BINN_LIST:
  case BINN_MAP:
  case BINN_OBJECT:
    return 1;
  default:
    return 0;
  }

}

/*************************************************************************************/