# tinyCBOR Example

This directory has sample CBOR data (`testcbor`) that can be used with the example. The CBOR data resembles the following CBOR (based on [cbor.me diagnostic notation](http://cbor.me/)):

```
[ 1, [2, 3, 4]]
```

To run the example, be sure the test data is in the same directory as the example executable and add `testcbor` as an argument to the executable.

Example:

```bash
./examples/simplereader testcbor
```
