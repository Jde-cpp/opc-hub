#pragma once
#include <functional>
#include <jde/fwk/exceptions/IOException.h>

#define Φ Γ auto
namespace Jde::IO::Records{
	using Day = std::chrono::year_month_day;
	//<yyyy>/<m>/<d>, as the log's archive names a day.
	Φ DayDirectory( Day day )ι->fs::path;
	//Each day that has a directory under root, newest first, until visit returns true.  Throws when a directory can't be
	//listed.
	Φ Walk( const fs::path& root, SL sl, const std::function<bool( Day )>& visit )ε->void;
	//Every day under root whose directory holds fileName, oldest first:  the directory walk, so a read asked for a range of
	//days opens only those that are there.  A day whose file it can't tell of, in a directory it can't search say, is
	//among them, so a read that reaches it fails as it opens it.
	Φ Days( const fs::path& root, sv fileName, SRCE )ε->vector<Day>;
}
#undef Φ