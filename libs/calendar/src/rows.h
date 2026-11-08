#pragma once
namespace Jde::Calendar{
	//A time column of a row, which is null or absent until it is set.
	Ξ findTime( const jobject& row, sv key, SL sl )ε->optional<TimePoint>{
		const auto p = row.if_contains( key );
		return p && !p->is_null() ? optional<TimePoint>{ Chrono::ToTimePoint(Json::AsString(*p, sl), sl) } : nullopt;
	}
}