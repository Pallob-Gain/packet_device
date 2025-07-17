#ifndef __COMMUNICATION_FLAGS_
#define __COMMUNICATION_FLAGS_




#define TRANSFER_DATA_BUFFER_SIG 0x2A

#define BUFFER_TEXT_RESPNOSE 0x5E
#define BUFFER_PARAM_RESPNOSE 0x5F
#define BUFFER_ARRY_RESPNOSE 0x60

#define DATA_TYPE_UINT64_T 1
#define DATA_TYPE_INT64_T 2
#define DATA_TYPE_UINT32_T 3
#define DATA_TYPE_INT32_T 4
#define DATA_TYPE_UINT16_T 5
#define DATA_TYPE_INT16_T 6
#define DATA_TYPE_UINT8_T 7
#define DATA_TYPE_INT8_T 8
#define DATA_TYPE_INT 9
#define DATA_TYPE_UINT 10
#define DATA_TYPE_FLOAT 11
#define DATA_TYPE_DOUBLE 12
#define DATA_TYPE_LONG 13
#define DATA_TYPE_ULONG 14
#define DATA_TYPE_STRING 15
#define DATA_TYPE_BOOL 16
#define DATA_TYPE_NULL 17
#define DATA_TYPE_VOID 0

typedef uint8_t null_type;

template<typename T>
uint8_t getTypeID() {
  if (std::is_same<T, uint64_t>::value) {
    return DATA_TYPE_UINT64_T;
  } else if (std::is_same<T, int64_t>::value) {
    return DATA_TYPE_INT64_T;
  } else if (std::is_same<T, uint32_t>::value) {
    return DATA_TYPE_UINT32_T;
  } else if (std::is_same<T, int32_t>::value) {
    return DATA_TYPE_INT32_T;
  } else if (std::is_same<T, uint16_t>::value) {
    return DATA_TYPE_UINT16_T;
  } else if (std::is_same<T, int16_t>::value) {
    return DATA_TYPE_INT16_T;
  } else if (std::is_same<T, uint8_t>::value) {
    return DATA_TYPE_UINT8_T;
  } else if (std::is_same<T, int8_t>::value) {
    return DATA_TYPE_INT8_T;
  } else if (std::is_same<T, int>::value) {
    return DATA_TYPE_INT;
  } else if (std::is_same<T, unsigned int>::value) {
    return DATA_TYPE_UINT;
  } else if (std::is_same<T, float>::value) {
    return DATA_TYPE_FLOAT;
  } else if (std::is_same<T, double>::value) {
    return DATA_TYPE_DOUBLE;
  } else if (std::is_same<T, long>::value) {
    return DATA_TYPE_LONG;
  } else if (std::is_same<T, unsigned long>::value) {
    return DATA_TYPE_ULONG;
  } else if (std::is_same<T, String>::value) {
    return DATA_TYPE_STRING;
  } else if (std::is_same<T, bool>::value) {
    return DATA_TYPE_BOOL;
  } else if (std::is_same<T, null_type>::value) {
    return DATA_TYPE_NULL;
  } else return DATA_TYPE_VOID;
}

#endif
