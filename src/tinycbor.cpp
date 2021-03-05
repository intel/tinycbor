#include <Arduino.h>
#include <tinycbor.h>
#include <stdarg.h>
#include <cborjson.h>

#ifdef ARDUINO
static void tinycbor_initializer() {
  TinyCBOR.Encoder.set_encoders(TinyCBOR.encoders, (sizeof(TinyCBOR.encoders)/sizeof(TinyCBOR.encoders[0])) );
  TinyCBOR.Parser.set_values(TinyCBOR.values, (sizeof(TinyCBOR.values)/sizeof(TinyCBOR.values[0])) );
};

struct TinyCBORPlaceHolder<TINYCBOR_MAXDEPTH> TinyCBOR = { tinycbor_initializer, {}, {}, {}, {}, {} };

static CborError print_printf(void *out, const char *format, ...)
{
#ifdef ARDUINO_API_VERSION
  arduino::Print* print = reinterpret_cast<arduino::Print*>(out);
#else
  Print* print = reinterpret_cast<Print*>(out);
#endif

  char buf[256];
  int len;

  va_list ap;
  va_start(ap, format);

  len = vsnprintf(buf, sizeof(buf), format, ap);
  print->write(buf, len);

  va_end(ap);

  return len < 0 ? CborErrorIO : CborNoError;
}

int TinyCBOREncoder::create_array(size_t length)
{
  if(mNestLevel+1 >= mDepth) return CborErrorInternalError;
  CborError err = cbor_encoder_create_array(&mEncoders[mNestLevel], &mEncoders[mNestLevel+1], length);
  mNestLevel++;
  return err;
}

int TinyCBOREncoder::create_map(size_t length)
{
  if(mNestLevel+1 >= mDepth) return CborErrorInternalError;
  CborError err = cbor_encoder_create_map(&mEncoders[mNestLevel], &mEncoders[mNestLevel+1], length);
  mNestLevel++;
  return err;
}

int TinyCBOREncoder::close_container()
{
  if(mNestLevel == 0) return CborErrorInternalError;
  CborError err = cbor_encoder_close_container(&mEncoders[mNestLevel-1], &mEncoders[mNestLevel]);
  mNestLevel--;
  return err;
}

int TinyCBORParser::enter_container()
{
  if(mNestLevel+1 >= mDepth) { return _set_err(CborErrorInternalError); }
  mNestLevel++;
  int ret = _set_err(cbor_value_enter_container(&mValues[mNestLevel-1], &mValues[mNestLevel]));
  set_ismapkey();
  return ret;
}

int TinyCBORParser::leave_container()
{
  if(mNestLevel == 0) { return _set_err(CborErrorInternalError); }
  mNestLevel--;
  set_ismapkey();
  return _set_err(cbor_value_leave_container(&mValues[mNestLevel], &mValues[mNestLevel+1]));
}

#ifdef ARDUINO_API_VERSION
int TinyCBORParser::pretty_print(arduino::Print& print, int flags)
#else
int TinyCBORParser::pretty_print(Print& print, int flags)
#endif
{
  return cbor_value_to_pretty_stream(print_printf, &print, &mValues[mNestLevel], flags);
}

#ifdef ARDUINO_API_VERSION
int TinyCBORParser::to_json(arduino::Print& print, int flags)
#else
int TinyCBORParser::to_json(Print& print, int flags)
#endif
{
  return cbor_value_to_json_stream(print_printf, &print, &mValues[mNestLevel], flags);
}

#endif
