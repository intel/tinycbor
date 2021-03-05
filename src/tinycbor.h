#ifndef TINYCBOR_H__
#define TINYCBOR_H__

#ifdef ARDUINO

#include <cbor.h>
#include <cborjson.h>

#ifndef TINYCBOR_MAXDEPTH
#define TINYCBOR_MAXDEPTH 4
#endif

/******************************************************
 * This conditional branch is for supporting new APIs
 * that use the arduino namespace.
 * It is assume that Arduino.h is already loaded.
 * Including this file from cpp files,
 * Arduino.h may not be loaded.
 * It it need to load Arduino.h befor including
 * this file in such case.
******************************************************/
#ifdef ARDUINO_API_VERSION
namespace arduino {
class Print;
}
#else
class Print;
#endif

class TinyCBOREncoder {
  CborEncoder *mEncoders;
  uint8_t* mBuffer;
  uint32_t mNestLevel;
  size_t mDepth;

public:
  TinyCBOREncoder() {}
  TinyCBOREncoder(CborEncoder* encoders, size_t len) { set_encoders(encoders, len); }

  inline void set_encoders(CborEncoder* encoders, size_t len) { mEncoders = encoders; mDepth = len; }

  inline void init(uint8_t *buffer, size_t size, int flags=0) { mBuffer = buffer; cbor_encoder_init(&mEncoders[mNestLevel], buffer, size, flags); }
  inline size_t get_buffer_size() { return cbor_encoder_get_buffer_size(&mEncoders[mNestLevel], mBuffer); }
  inline size_t get_extra_bytes_needed() { return cbor_encoder_get_extra_bytes_needed(&mEncoders[mNestLevel]); }

  inline uint8_t* get_buffer() { return mBuffer; }

  inline int encode_int(int64_t value) { return cbor_encode_int(&mEncoders[mNestLevel], value); }
  inline int encode_uint(uint64_t value) { return cbor_encode_uint(&mEncoders[mNestLevel], value); }
  inline int encode_negative_int(uint64_t absolute_value) { return cbor_encode_negative_int(&mEncoders[mNestLevel], absolute_value); }
  inline int encode_byte_string(const uint8_t *string, size_t length) { return cbor_encode_byte_string(&mEncoders[mNestLevel], string, length); }
  inline int encode_text_string(const char *string, size_t length) { return cbor_encode_text_string(&mEncoders[mNestLevel], string, length); }
  inline int encode_text_stringz(const char *string) { return cbor_encode_text_stringz(&mEncoders[mNestLevel], string); }
  inline int encode_tag(CborTag tag) { return cbor_encode_tag(&mEncoders[mNestLevel], tag); }
  inline int encode_simple_value(uint8_t value) { return cbor_encode_simple_value(&mEncoders[mNestLevel], value); }
  inline int encode_boolean(bool value) { return cbor_encode_boolean(&mEncoders[mNestLevel], value); }
  inline int encode_null() { return cbor_encode_null(&mEncoders[mNestLevel]); }
  inline int encode_undefined() { return cbor_encode_undefined(&mEncoders[mNestLevel]); }
  inline int encode_floating_point(CborType fpType, const void *value) { return cbor_encode_floating_point(&mEncoders[mNestLevel], fpType, value); }
  inline int encode_half_float(const void *value) { return cbor_encode_half_float(&mEncoders[mNestLevel], value); }
  inline int encode_float(float value) { return cbor_encode_float(&mEncoders[mNestLevel], value); }
  inline int encode_double(double value) { return cbor_encode_double(&mEncoders[mNestLevel], value); }

  int create_array(size_t length);
  int create_map(size_t length);
  int close_container();
};

class TinyCBORParser {
  CborParser mParser;
  CborValue *mValues;
  uint32_t mNestLevel;
  CborError mError;
  size_t mDepth;
  bool mIsMapKey;

  template<typename T>
  inline T _clr_err(T val) { mError = CborNoError; return val; }
  inline CborError _set_err(CborError err) { mError = err; return err; }

  inline CborError _set_err_adv(CborError err) { mError = err; if(!err) {_set_err(cbor_value_advance(&mValues[mNestLevel])); set_ismapkey();} return mError; }
  inline CborError _set_err_advfix(CborError err) { mError = err; if(!err) {_set_err(cbor_value_advance_fixed(&mValues[mNestLevel])); set_ismapkey(); } return mError; }
  inline void set_ismapkey() {
    if( mNestLevel != 0 && !mIsMapKey
     && cbor_value_is_map(&mValues[mNestLevel-1])
     && cbor_value_is_text_string(&mValues[mNestLevel])) {
      mIsMapKey = true;
    }
    else {
      mIsMapKey = false;
    }
  }

public:
  TinyCBORParser() {}
  TinyCBORParser(CborValue* values, size_t len) { set_values(values, len); }

  inline void set_values(CborValue* values, size_t len) { mValues = values; mDepth = len; }

  int get_error() { return mError; }
  uint32_t get_depth() { return mNestLevel; }
  bool is_key() { return mIsMapKey; }

  CborValue* get_value() { return &mValues[mNestLevel]; }

  bool is_in_map() { return (mNestLevel != 0 && cbor_value_is_map(&mValues[mNestLevel-1]) ); }
  bool is_in_array() { return (mNestLevel != 0 && cbor_value_is_array(&mValues[mNestLevel-1]) ); }

  inline int init(const uint8_t *buffer, size_t size, uint32_t flags) { return _clr_err(cbor_parser_init(buffer, size, flags, &mParser, &mValues[mNestLevel])); }

  inline bool at_end() { return _clr_err(cbor_value_at_end(&mValues[mNestLevel])); }
  inline bool at_end_of_data() { return (mNestLevel == 0) && at_end(); }
  inline bool at_end_of_container() { return (mNestLevel != 0) && at_end(); }

  inline const uint8_t* get_next_byte() { return _clr_err(cbor_value_get_next_byte(&mValues[mNestLevel])); }
  inline int advance_fixed() { return _set_err(cbor_value_advance_fixed(&mValues[mNestLevel])); }
  inline int advance() { return _set_err(cbor_value_advance(&mValues[mNestLevel])); }

  inline int get_type() { return _clr_err(cbor_value_get_type(&mValues[mNestLevel])); }

  inline bool is_integer() { return _clr_err(cbor_value_is_integer(&mValues[mNestLevel])); }
  inline bool is_unsigned_integer() { return _clr_err(cbor_value_is_unsigned_integer(&mValues[mNestLevel])); }
  inline bool is_negative_integer() { return _clr_err(cbor_value_is_negative_integer(&mValues[mNestLevel])); }
  inline bool is_byte_string() { return _clr_err(cbor_value_is_byte_string(&mValues[mNestLevel])); }
  inline bool is_text_string() { return _clr_err(cbor_value_is_text_string(&mValues[mNestLevel])); }
  inline bool is_container() { return _clr_err(cbor_value_is_container(&mValues[mNestLevel])); }
  inline bool is_array() { return _clr_err(cbor_value_is_array(&mValues[mNestLevel])); }
  inline bool is_map() { return _clr_err(cbor_value_is_map(&mValues[mNestLevel])); }
  inline bool is_tag() { return _clr_err(cbor_value_is_tag(&mValues[mNestLevel])); }
  inline bool is_simple_type() { return _clr_err(cbor_value_is_simple_type(&mValues[mNestLevel])); }
  inline bool is_boolean() { return _clr_err(cbor_value_is_boolean(&mValues[mNestLevel])); }
  inline bool is_null() { return _clr_err(cbor_value_is_null(&mValues[mNestLevel])); }
  inline bool is_undefined() { return _clr_err(cbor_value_is_undefined(&mValues[mNestLevel])); }
  inline bool is_half_float() { return _clr_err(cbor_value_is_half_float(&mValues[mNestLevel])); }
  inline bool is_float() { return _clr_err(cbor_value_is_float(&mValues[mNestLevel])); }
  inline bool is_double() { return _clr_err(cbor_value_is_double(&mValues[mNestLevel])); }
  inline bool is_length_known() { return _clr_err(cbor_value_is_length_known(&mValues[mNestLevel])); }
  inline bool is_valid() { return _clr_err(cbor_value_is_valid(&mValues[mNestLevel])); }

  inline int get_int() {int ret; return _set_err_advfix(cbor_value_get_int(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline int get_int_checked() {int ret; return _set_err_advfix(cbor_value_get_int_checked(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline int64_t get_int64() {int64_t ret; return _set_err_advfix(cbor_value_get_int64(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline int64_t get_int64_checked() {int64_t ret; return _set_err_advfix(cbor_value_get_int64_checked(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline uint64_t get_uint64() {uint64_t ret; return _set_err_advfix(cbor_value_get_uint64(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline uint64_t get_raw_integer() {uint64_t ret; return _set_err_advfix(cbor_value_get_raw_integer(&mValues[mNestLevel], &ret)) ? 0 : ret; }

  inline size_t copy_text_string(char *buf, size_t len) { return _set_err_adv(cbor_value_copy_text_string(&mValues[mNestLevel], buf, &len, NULL)) ? 0 : len; }
  inline size_t copy_byte_string(uint8_t *buf, size_t len) { return _set_err_adv(cbor_value_copy_byte_string(&mValues[mNestLevel], buf, &len, NULL)) ? 0 : len; }
  inline size_t get_string_length() {size_t len; return _set_err(cbor_value_get_string_length(&mValues[mNestLevel], &len)) ? 0 : len; }
  inline bool text_string_equals(const char *string) { bool ret; return _set_err(cbor_value_text_string_equals(&mValues[mNestLevel], string, &ret)) ? 0 : ret; }

  inline int get_tag() {CborTag ret; return _set_err_advfix(cbor_value_get_tag(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline int skip_tag() { return _set_err(cbor_value_skip_tag(&mValues[mNestLevel])); }

  inline bool get_boolean() {bool ret; return _set_err_advfix(cbor_value_get_boolean(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline uint8_t get_simple_type() {uint8_t ret; return _set_err_advfix(cbor_value_get_simple_type(&mValues[mNestLevel], &ret)) ? 0 : ret; }

  inline int get_half_float(void *ret) { return _set_err(cbor_value_get_half_float(&mValues[mNestLevel], ret)); }
  inline float get_float() {float ret; return _set_err_advfix(cbor_value_get_float(&mValues[mNestLevel], &ret)) ? 0 : ret; }
  inline double get_double() {double ret; return _set_err_advfix(cbor_value_get_double(&mValues[mNestLevel], &ret)) ? 0 : ret; }

  int enter_container();
  int leave_container();
  inline size_t get_array_length() {size_t len; return _set_err(cbor_value_get_array_length(&mValues[mNestLevel], &len)) ? 0 : len; }
  inline size_t get_map_length() {size_t len; return _set_err(cbor_value_get_map_length(&mValues[mNestLevel], &len)) ? 0 :len; }

  inline int validate_basic() { return _set_err(cbor_value_validate_basic(&mValues[mNestLevel])); }
  inline int validate(uint32_t flags) { return _set_err(cbor_value_validate(&mValues[mNestLevel], flags)); }

  inline nullptr_t get_null() { advance_fixed(); return nullptr; }
  inline int skip_undefined() { return advance_fixed(); }

#ifdef ARDUINO_API_VERSION
  int pretty_print(arduino::Print& print, int flags=CborPrettyDefaultFlags);
  int to_json(arduino::Print &print, int flags=CborConvertDefaultFlags);
#else
  int pretty_print(Print& print, int flags=CborPrettyDefaultFlags);
  int to_json(Print &print, int flags=CborConvertDefaultFlags);
#endif
};

class TinyCBORError {
public:
  static inline const char* to_string(int err) {
    return cbor_error_string(static_cast<CborError>(err));
  }
};

typedef void (*tinycbor_init_proc)(void);

template<int MAXDEPTH>
struct TinyCBORPlaceHolder {
  tinycbor_init_proc init;
  CborEncoder encoders[MAXDEPTH];
  CborValue values[MAXDEPTH];
  TinyCBOREncoder Encoder;
  TinyCBORParser Parser;
  TinyCBORError Error;
};

extern struct TinyCBORPlaceHolder<TINYCBOR_MAXDEPTH> TinyCBOR;

#endif //ARDUINO

#endif //TINYCBOR_H__

