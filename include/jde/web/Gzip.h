#pragma once

namespace Jde::Web{
	//A gzip stream inflated.  nullopt once the output passes `limit` (0: none) - checked as the buffer grows, so a body of a
	//few kilobytes that would expand to gigabytes stops at the cap instead of allocating them.  Malformed input throws.
	α Gunzip( sv compressed, uint limit=0, SRCE )ε->optional<string>;
	α IsGzip( sv contentEncoding )ι->bool;//any case, and the legacy x-gzip (RFC 9110 §8.4.1.3) - the one rule for requests and responses.
}