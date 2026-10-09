#pragma once

namespace Jde::Logging{
	//Routes abseil's own logging - and protobuf's, which logs through it - into the loggers under ELogTags::App.
	α AddAbseilSink()ι->void;
	α RemoveAbseilSink()ι->void;
}
