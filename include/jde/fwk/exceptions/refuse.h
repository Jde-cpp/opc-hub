#pragma once
//A refusal the client sees:  a BadRequest, from a function with an `sl` in a file that defines `_tags`.
#define REFUSE(...) throw Jde::Exception{ sl, {_tags, 0, EHttpStatus::BadRequest}, __VA_ARGS__ }
#define REFUSE_IF(condition, ...) if( condition ) REFUSE( __VA_ARGS__ )