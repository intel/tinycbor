#include <tinycbor.h>

// Error check and exiting.
#define CHECK_ERROR(x) {\
  if( (err = TinyCBOR.Parser.get_error()) != 0) {\
    err_line = __LINE__;\
    goto on_error;\
  }\
}

#define EXIT_ERROR(e) {\
  if( (err = e) != 0) {\
    err_line = __LINE__;\
    goto on_error;\
  }\
}

#define PRINT_AND_CHECK_ERROR(fmt, val, post) {\
  for(uint32_t i=0; i<(TinyCBOR.Parser.get_depth()*2); i++) { Serial.print(" "); }\
  Serial.print(fmt);\
  Serial.print(val);\
  Serial.print(post);\
  CHECK_ERROR(val)\
}

#ifndef ARDUINO_PRINT_64BIT_VALUE
#define CAST_UINT64(x) ((uint32_t)x)
#define CAST_INT64(x)  ((int32_t)x)
#else
#define CAST_UINT64(x) (x)
#define CAST_INT64(x)  (x)
#endif

const uint8_t cbor_encode_sample_output[] =
{
  0x89, 0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0x1b, 0x7f, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0x3b, 0x7f, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xfe, 0x44, 0x62, 0x79, 0x74,
  0x65, 0xd8, 0x23, 0x64, 0x5e, 0x2e, 0x2a, 0x24,
  0xa3, 0x74, 0x68, 0x74, 0x74, 0x70, 0x73, 0x3a,
  0x2f, 0x2f, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c,
  0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x2f, 0xfa, 0x7f,
  0x7f, 0xff, 0xff, 0x66, 0x64, 0x6f, 0x75, 0x62,
  0x6c, 0x65, 0xfa, 0x7f, 0x80, 0x00, 0x00, 0x63,
  0x6d, 0x61, 0x70, 0xa1, 0x64, 0x62, 0x6f, 0x6f,
  0x6c, 0xf5, 0xf6, 0xf7, 0xf8, 0xff
};

char strbuf[128];
uint8_t bytebuf[128];

void setup() {
  Serial.begin(115200);
  Serial.println("--- cbor_parse_sample start ---");
  Serial.println();
}

void loop() {
  int err;
  int err_line;

  TinyCBOR.init();

  // Pretty print.
  TinyCBOR.Parser.init(cbor_encode_sample_output, sizeof(cbor_encode_sample_output), 0);
  Serial.println("---  pretty print example ---");
  TinyCBOR.Parser.pretty_print(Serial);
  Serial.println();

  // Convert json.
  TinyCBOR.Parser.init(cbor_encode_sample_output, sizeof(cbor_encode_sample_output), 0);
  Serial.println("------  json example  -------");
  TinyCBOR.Parser.to_json(Serial);
  Serial.println();
  Serial.println("-----------------------------");
  Serial.println();

  /////////////////////
  // Parse CBOR data //
  /////////////////////

  TinyCBOR.Parser.init(cbor_encode_sample_output, sizeof(cbor_encode_sample_output), 0);

  // TinyCBOR.Parser internally has iterator.
  // Itereator is forwarding with data reading or skipping.
  while(!TinyCBOR.Parser.at_end_of_data()) {


    // Process each data type
    int ty = TinyCBOR.Parser.get_type();
    switch(ty) {

    ///////////////////////////////////
    // array,map,tag and text string constructs a data structure.
    ///////////////////////////////////

    // If container type shown, move into container substructure.
    case CborArrayType:
    case CborMapType:
      if(TinyCBOR.Parser.is_array() ) {
        PRINT_AND_CHECK_ERROR("[ # array length=", TinyCBOR.Parser.get_array_length(), "\n");
      } else if(TinyCBOR.Parser.is_map()) {
        PRINT_AND_CHECK_ERROR("{ # map length", TinyCBOR.Parser.get_map_length(), "\n");
      }
      TinyCBOR.Parser.enter_container();
      break;

    // CborInvalidType(0xff) is equivalent to the break byte.
    // Break byte is put at end of container.
    //
    // On reached end of container, exit from the container.
    case CborInvalidType:
      if(TinyCBOR.Parser.at_end_of_container() ) {

        bool inmap = TinyCBOR.Parser.is_in_map();
        TinyCBOR.Parser.leave_container();
        if(inmap) {
          PRINT_AND_CHECK_ERROR("}", "", "\n");
        }
        else {
          PRINT_AND_CHECK_ERROR("]", "", "\n");
        }
      }
      else {
        // Really unknown type, Unexpectable!
        EXIT_ERROR(CborErrorUnexpectedBreak);
      }
      break;

    // text string is also use as key of a map.
    // is_key() tells current value is key or not.
    case CborTextStringType:
      {
        size_t str_len = 0;
        CHECK_ERROR(str_len = TinyCBOR.Parser.get_string_length());
        if(str_len > sizeof(strbuf) ) {
          EXIT_ERROR(CborErrorOutOfMemory);
        }
        bool iskey = TinyCBOR.Parser.is_key();
        str_len = TinyCBOR.Parser.copy_text_string(strbuf, sizeof(strbuf));
        if(iskey) {
          PRINT_AND_CHECK_ERROR("[key:", strbuf, "] ");
        }
        else {
          PRINT_AND_CHECK_ERROR("", strbuf, " # text string\n");
        }
      }
      break;

    // Tag type is not count as container element.
    // It is use to decorate next appeared element.
    case CborTagType:
      PRINT_AND_CHECK_ERROR("<", (uint32_t)TinyCBOR.Parser.get_tag(), ">");
      break;

    //////////////////////////////////////////////////////////
    // Other value types not have structually requiremnet.
    //
    //////////////////////////////////////////////////////////

    case CborByteStringType:
      {
        size_t str_len = 0;
        CHECK_ERROR(str_len = TinyCBOR.Parser.get_string_length());
  
        if(str_len > sizeof(bytebuf) ) {
          EXIT_ERROR(CborErrorOutOfMemory);
        }
        
        str_len = TinyCBOR.Parser.copy_byte_string(bytebuf, sizeof(bytebuf));
        PRINT_AND_CHECK_ERROR("", (char*)bytebuf, " # byte string\n");
      }
      break;

    case CborIntegerType:
      //
      // INT64_MIN ...., 0 ..... INT64_MAX ...... UINT64_MAX
      // [ negative int ]
      // [ ------------ int ------------ ]
      //                 [ --------- unsingned int ------- ]
      //
      if(TinyCBOR.Parser.is_unsigned_integer()) {
        PRINT_AND_CHECK_ERROR("", CAST_UINT64(TinyCBOR.Parser.get_uint64()), " # unsigned integer\n");
      } else if(TinyCBOR.Parser.is_negative_integer()) {
        PRINT_AND_CHECK_ERROR("", CAST_INT64(TinyCBOR.Parser.get_int64()), " # negative integer\n");
      } else if(TinyCBOR.Parser.is_integer()){
        PRINT_AND_CHECK_ERROR("", CAST_INT64(TinyCBOR.Parser.get_int64()), " # integer\n");
      }
      break;

    case CborSimpleType:
      PRINT_AND_CHECK_ERROR("", TinyCBOR.Parser.get_simple_type(), " # simple type\n");
      break;

    case CborBooleanType:
      PRINT_AND_CHECK_ERROR("", TinyCBOR.Parser.get_boolean(), " # bool \n");
      break;

    case CborFloatType:
      PRINT_AND_CHECK_ERROR("", TinyCBOR.Parser.get_float(), " # float\n");
      break;

    case CborDoubleType:
      PRINT_AND_CHECK_ERROR("", TinyCBOR.Parser.get_double(), " # double\n");
      break;

    case CborHalfFloatType:
      break;

    case CborNullType:
      TinyCBOR.Parser.get_null();
      PRINT_AND_CHECK_ERROR("", "null", " # null \n");
      break;

    case CborUndefinedType:
      TinyCBOR.Parser.skip_undefined();
      PRINT_AND_CHECK_ERROR("", "", " # undefined \n");
      break;

    default:
      EXIT_ERROR(CborUnknownError);
    }
  }

on_error:
  // Detect error in CHECK_ERROR macro, jump to here.
  if(err) {
    Serial.print("Error: ");
    Serial.print(err);
    Serial.print(" ");
    Serial.print(TinyCBOR.Error.to_string(err));
    Serial.print(" raise at line:");
    Serial.print(err_line);
    Serial.println();
  }

  Serial.println("--- end cbor_parse_sample ---");
  // Stop this program.
  while(true);
}
