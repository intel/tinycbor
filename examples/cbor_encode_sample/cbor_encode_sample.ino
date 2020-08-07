/**
 * CBOR encode sample
 * 
 */
#include <tinycbor.h>
#include <float.h>

// Error check and exiting.
#define CHECK_ERROR(proc) {\
  if( (err = proc) != 0) {\
    err_line = __LINE__;\
    goto on_error;\
  }\
}

uint8_t encode_buffer[1024];

void setup() {
  Serial.begin(115200);
  Serial.println("--- cbor_encode_sample start ---");
}

void loop() {
  int err = 0;
  int err_line = 0;

  // Initialize TinyCBOR library.
  TinyCBOR.init();

  // Assign buffer to encoder.
  TinyCBOR.Encoder.init(encode_buffer, sizeof(encode_buffer));

  /////////////////////////
  // Start CBOR encoding //
  /////////////////////////
  
  // Typically, the first element is a array or a map.
  CHECK_ERROR(TinyCBOR.Encoder.create_array(9));
  {
    CHECK_ERROR(TinyCBOR.Encoder.encode_uint(UINT64_MAX));
    CHECK_ERROR(TinyCBOR.Encoder.encode_int(INT64_MAX));
    CHECK_ERROR(TinyCBOR.Encoder.encode_negative_int(INT64_MAX));

    CHECK_ERROR(TinyCBOR.Encoder.encode_byte_string((uint8_t*)"byte", 4));

    // Tag is not count as array elements.
    CHECK_ERROR(TinyCBOR.Encoder.encode_tag(CborRegularExpressionTag));
    CHECK_ERROR(TinyCBOR.Encoder.encode_text_stringz("^.*$"));

    // create_array() and create_map() are increment nest level internally.
    // Allowed nest level is decided by value of TINYCBOR_MAXDEPTH .
    CHECK_ERROR(TinyCBOR.Encoder.create_map(3));
    {
      CHECK_ERROR(TinyCBOR.Encoder.encode_text_stringz("https://example.com/"));
      CHECK_ERROR(TinyCBOR.Encoder.encode_float(FLT_MAX));
      CHECK_ERROR(TinyCBOR.Encoder.encode_text_string("double", 6));
      CHECK_ERROR(TinyCBOR.Encoder.encode_float(DBL_MAX));

      // Map can contains nested maps.
      CHECK_ERROR(TinyCBOR.Encoder.encode_text_stringz("map"));
      CHECK_ERROR(TinyCBOR.Encoder.create_map(1));
      {
        CHECK_ERROR(TinyCBOR.Encoder.encode_text_stringz("bool"));
        CHECK_ERROR(TinyCBOR.Encoder.encode_boolean(true));
      }
      CHECK_ERROR(TinyCBOR.Encoder.close_container());
        
    }
    CHECK_ERROR(TinyCBOR.Encoder.close_container());
    // close_container() called by paired with create_array() and create_map()
    // It is counting down internal nest level.

    CHECK_ERROR(TinyCBOR.Encoder.encode_null());
    CHECK_ERROR(TinyCBOR.Encoder.encode_undefined());
    CHECK_ERROR(TinyCBOR.Encoder.encode_simple_value(0xFF));
  }
  // Close first element to finish encoding.
  CHECK_ERROR(TinyCBOR.Encoder.close_container());

  ///////////////////////
  // End CBOR encoding //
  ///////////////////////

  {
    // Encoded data can retrieve with get_buffer() and get_buffer_size().
    uint8_t* buf = TinyCBOR.Encoder.get_buffer(); 
    size_t sz = TinyCBOR.Encoder.get_buffer_size();
  
    Serial.print("encoded data size = ");
    Serial.println(sz);
  
    Serial.println("const char cbor_encode_sample_output[] =");
    Serial.print("{");
    for(uint32_t i=0; i<sz; i++) {
      if(i%8 == 0) {
        Serial.println();
        Serial.print("  ");
      }
      Serial.print("0x");
      if(buf[i] < 0x10) Serial.print("0");
      Serial.print(buf[i], HEX);
      if(i!=sz-1) Serial.print(", ");
    }
    Serial.println();
    Serial.println("};");
    
    Serial.println();
    err = 0;
  }

on_error:
  // Detect error in CHECK_ERROR macro, jump to here.
  if(err) {
    Serial.print("Error: ");
    Serial.print(err);
    Serial.print(" raise at line:");
    Serial.print(err_line);
    Serial.println();
  }

  Serial.println("--- end cbor_encode_sample ---");
  // Stop this program.
  while(true);
}

