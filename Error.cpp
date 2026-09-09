
////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// Error.cpp - Enumerator names for Result codes.
////////////////////////////////////////////////////////////////////////////////

#include "Error.hpp"

////////////////////////////////////////////////////////////////////////////////
namespace Flat {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// string_Result
//  Enumerator name for a Result code (e.g. "ERROR_OVERFLOW"), for logging.
///////////////////////////////////////////////////////
[[gnu::cold]] const char* string_Result(Result result) {
  switch (result) {
    case SUCCESS:                     return "SUCCESS";
    case ERROR_OUT_OF_MEMORY:         return "ERROR_OUT_OF_MEMORY";
    case ERROR_INITIALIZATION_FAILED: return "ERROR_INITIALIZATION_FAILED";
    case ERROR_LAYER_NOT_PRESENT:     return "ERROR_LAYER_NOT_PRESENT";
    case ERROR_EXTENSION_NOT_PRESENT: return "ERROR_EXTENSION_NOT_PRESENT";
    case ERROR_FEATURE_NOT_PRESENT:   return "ERROR_FEATURE_NOT_PRESENT";
    case ERROR_LIMIT_REACHED:         return "ERROR_LIMIT_REACHED";
    case ERROR_UNKNOWN:               return "ERROR_UNKNOWN";
    case BUFFER_EMPTY:                return "BUFFER_EMPTY";
    case BUFFER_WRAPPED:              return "BUFFER_WRAPPED";
    case ERROR_OVERFLOW:              return "ERROR_OVERFLOW";
    case REQUEST_INITIALIZE:          return "REQUEST_INITIALIZE";
    case REQUEST_EXIT:                return "REQUEST_EXIT";
    case NOT_READY:                   return "NOT_READY";
    case NOT_CONNECTED:               return "NOT_CONNECTED";
    case CONNECTION_LOST:             return "CONNECTION_LOST";
    case DEGRADED:                    return "DEGRADED";
    case CREATED:                     return "CREATED";
    case NOT_FOUND:                   return "NOT_FOUND";
    case TIMEOUT:                     return "TIMEOUT";
    case ABSENT_VALUE:                return "ABSENT_VALUE";
    case ERROR_NOT_FOUND:             return "ERROR_NOT_FOUND";
    case ERROR_INVALID_ARGUMENT:      return "ERROR_INVALID_ARGUMENT";
    case ERROR_IO:                    return "ERROR_IO";
    case ERROR_JS_EXCEPTION:          return "ERROR_JS_EXCEPTION";
    case ERROR_MALFORMED:             return "ERROR_MALFORMED";
    case ERROR_INSUFFICIENT_SPACE:    return "ERROR_INSUFFICIENT_SPACE";
    default:                          return "RESULT N/A";
  }
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat
////////////////////////////////////////////////////////////////////////////////
