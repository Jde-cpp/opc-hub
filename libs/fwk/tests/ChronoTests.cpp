#include <jde/fwk/chrono.h>

#define let const auto

namespace Jde::Tests{
	using namespace std::chrono;

	TEST( ChronoTests, DurationRoundTripMonths ){
		let d = duration_cast<Duration>( months{5} );
		EXPECT_EQ( Chrono::ToString(d), "P5M" );
		EXPECT_EQ( Chrono::ToDuration(Chrono::ToString(d)), d );
	}

	TEST( ChronoTests, DurationDays ){
		EXPECT_EQ( Chrono::ToDuration(sv{"P4D"}), duration_cast<Duration>(days{4}) );
	}

	TEST( ChronoTests, DurationRoundTripMixed ){
		let d = duration_cast<Duration>(years{2}) + duration_cast<Duration>(months{3}) + duration_cast<Duration>(days{4}) + hours{5} + minutes{6} + seconds{7};
		let s = Chrono::ToString(d);
		EXPECT_EQ( Chrono::ToDuration(s), d ) << "str=" << s;
	}

	// Regression: the sub-second remainder was emitted with no unit suffix ("PT1S500"), which ToDuration dropped.
	TEST( ChronoTests, DurationRoundTripSubSecond ){
		let d = duration_cast<Duration>( milliseconds{1500} );
		EXPECT_EQ( Chrono::ToString(d), "PT1.5S" );
		EXPECT_EQ( Chrono::ToDuration(Chrono::ToString(d)), d );
		EXPECT_EQ( Chrono::ToString(duration_cast<Duration>(milliseconds{-1050})), "PT-1.05S" );
		EXPECT_EQ( Chrono::ToDuration(sv{"PT-1.05S"}), duration_cast<Duration>(milliseconds{-1050}) );
		EXPECT_EQ( Chrono::ToDuration(Chrono::ToString(duration_cast<Duration>(milliseconds{50}))), duration_cast<Duration>(milliseconds{50}) );
	}

	// Precision must trim the rendered string, not zero-fill it: %T's digit count comes from the time_point's period,
	// so rounding the value back into TimePoint left "…:05.123000Z" for millisecond precision.
	TEST( ChronoTests, IsoStringPrecisionTrimsOutput ){
		let tp = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5, duration_cast<Duration>(microseconds{123456}) );
		EXPECT_EQ( ToIsoString<milliseconds>(tp), "2024-01-02T03:04:05.123Z" );
		EXPECT_EQ( ToIsoString<seconds>(tp), "2024-01-02T03:04:05Z" );
		EXPECT_EQ( ToIsoString<minutes>(tp), "2024-01-02T03:04Z" );
		EXPECT_EQ( ToIsoString<hours>(tp), "2024-01-02T03Z" );
		EXPECT_EQ( ToIsoString<days>(tp), "2024-01-02" ) << "a bare date carries no zone designator";
		EXPECT_EQ( ToIsoString<microseconds>(tp), "2024-01-02T03:04:05.123456Z" );
		//Precision defaults to void: the time_point's own precision, untouched - and that period is the standard
		//library's, not ours (1µs on libc++, 100ns on the MSVC STL), so only the leading digits are portable.
		let dflt = ToIsoString( tp );
		EXPECT_TRUE( dflt.starts_with("2024-01-02T03:04:05.123456") ) << dflt;
		EXPECT_TRUE( dflt.ends_with("Z") ) << dflt;
		EXPECT_EQ( ToIsoString<Duration>(tp), dflt ) << "void must render exactly what the time_point's own period does";
		EXPECT_EQ( ToIsoString<>(tp), dflt );
	}

	// steady_clock has no calendar, so it is converted to Clock first - it used to need an explicit specialization and
	// now falls out of the one template, at every precision rather than just the default.
	TEST( ChronoTests, IsoStringSteadyClock ){
		let steady = steady_clock::now();
		EXPECT_EQ( ToIsoString(steady).size(), ToIsoString(Clock::now()).size() );
		EXPECT_EQ( ToIsoString<days>(steady).size(), 10u ) << "yyyy-mm-dd";
		EXPECT_EQ( ToIsoString<seconds>(steady).size(), 20u ) << "yyyy-mm-ddThh:mm:ssZ";
	}

	// floor, not round: a cert expiring at 18:00 expires *that* day, and a rendered expiration must never read later
	// than the instant it stands for.
	TEST( ChronoTests, IsoStringPrecisionTruncates ){
		let evening = Chrono::ToTimePoint( 2024, 1, 2, 18, 44, 59, duration_cast<Duration>(milliseconds{900}) );
		EXPECT_EQ( ToIsoString<days>(evening), "2024-01-02" );
		EXPECT_EQ( ToIsoString<hours>(evening), "2024-01-02T18Z" );
		EXPECT_EQ( ToIsoString<minutes>(evening), "2024-01-02T18:44Z" );
		EXPECT_EQ( ToIsoString<seconds>(evening), "2024-01-02T18:44:59Z" );
	}

	TEST( ChronoTests, IsoStringRoundTrips ){
		let tp = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5 );
		EXPECT_EQ( Chrono::ToTimePoint(ToIsoString<seconds>(tp)), tp );
		EXPECT_EQ( Chrono::ToTimePoint(ToIsoString<milliseconds>(tp)), tp );
	}

	TEST( ChronoTests, ToTimePointZulu ){
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05Z"), Chrono::ToTimePoint("2024-01-02T03:04:05") );
	}

	TEST( ChronoTests, ToTimePointOffset ){
		//+05:00 => local is 5h ahead of UTC, so UTC = 03:04:05 - 5h = 2024-01-01T22:04:05.
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05+05:00"), Chrono::ToTimePoint("2024-01-01T22:04:05") );
		//-03:00 => local is 3h behind UTC, so UTC = 03:04:05 + 3h = 06:04:05.
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05-03:00"), Chrono::ToTimePoint("2024-01-02T06:04:05") );
	}

	//every one of these must throw rather than come back as the epoch - an unparseable expiry reading as 1970 is a
	//permanent "expired" instead of an error someone can see.
	Ω parseFailure( sv iso )->string{
		try{
			Chrono::ToTimePoint( string{iso} );
			return {};
		}
		catch( const Exception& e ){ return e.what(); }
	}
	TEST( ChronoTests, ToTimePointRejectsGarbage ){
		for( let iso : {"2024-13-45", "", "not a date", "2024-01-02", "2024-01-02T03:04"} ){
			let what = parseFailure( iso );
			EXPECT_NE( what.find("Could not parse ISO time"), string::npos ) << "'" << iso << "' parsed as a time point, or threw something else: " << what;
		}
		//with and without a zone - the parse is tried both ways.
		EXPECT_NE( parseFailure("garbage-with-a-zone+05:00").find("Could not parse ISO time"), string::npos );
		EXPECT_NE( parseFailure("garbage-that-ends-in-a-zulu-charZ").find("Could not parse ISO time"), string::npos );
	}

	//ql-review3 #4: the zone offset was read with std::stoi, which throws std::invalid_argument - a logic_error, not a
	//runtime_error - so a junk offset escaped every `catch( runtime_error& )` in the callers and terminated the process
	//(QL::makeTimes' noexcept lambda, from an anonymous `providers(name:"2026-08-02T10:00:00+ab")`).  ToTimePoint only
	//throws Jde::Exception.  parseFailure catches Exception, so anything else still takes the suite down with it.
	//A bare sign and a non-numeric offset are rejected too:  stoi took any numeric prefix and read them as UTC.
	TEST( ChronoTests, ToTimePointRejectsAMalformedZone ){
		for( let iso : {"2026-08-02T10:00:00+ab", "2026-08-02T10:00:00-xy", "2026-08-02T10:00:00+", "2026-08-02T10:00:00+0a:00"} ){
			let what = parseFailure( iso );
			EXPECT_NE( what.find("Could not parse ISO"), string::npos ) << "'" << iso << "' parsed, or threw something else: " << what;
		}
		EXPECT_NO_THROW( Chrono::ToTimePoint("2026-08-02T10:00:00+05:00") ); //the control: a real offset still parses.
		EXPECT_NO_THROW( Chrono::ToTimePoint("2026-08-02T10:00:00.123456") );
		//the offset spellings that keep working:  ±hh, ±hhmm, ±hh:mm.
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05+05"), Chrono::ToTimePoint("2024-01-01T22:04:05") );
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05+0500"), Chrono::ToTimePoint("2024-01-01T22:04:05") );
	}

	//year_month_day::ok() is what separates a real calendar date from an arithmetic one; without it sys_days{ymd}
	//happily normalizes Feb 30 into March, so a bad date becomes a valid-looking time point a few days out.
	TEST( ChronoTests, InvalidYmdThrows ){
		EXPECT_THROW( Chrono::ToTimePoint(2023, 2, 29), Exception ) << "2023 is not a leap year";
		EXPECT_THROW( Chrono::ToTimePoint(2024, 2, 30), Exception );
		EXPECT_THROW( Chrono::ToTimePoint(2024, 13, 1), Exception );
		EXPECT_THROW( Chrono::ToTimePoint(2024, 4, 31), Exception );
		//...and the check is the calendar, not a blanket day>28: the leap day itself must go through.
		EXPECT_NO_THROW( Chrono::ToTimePoint(2024, 2, 29) );
		EXPECT_EQ( ToIsoString<days>(Chrono::ToTimePoint(2024, 2, 29)), "2024-02-29" );
	}

	//ToDuration is fed straight from config (Settings/json durations), so the leading 'P' check is the only thing
	//between a mistyped setting and a silently-zero duration - every token loop below it just breaks on what it
	//cannot read, which for "5M" would be immediately.
	TEST( ChronoTests, ToDurationRequiresP ){
		for( let iso : {"5M", "", "T5M", "p5M"} ){
			try{
				let d = Chrono::ToDuration( sv{iso} );
				ADD_FAILURE() << "'" << iso << "' parsed as " << Chrono::ToString( d );
			}
			catch( const Exception& e ){
				//only the stable half of the message: the rest of it is garbled and asserting on it would pin the bug
				//in place.  `is.get()` returns int, so the "{}{}" renders the offending character as its code and then
				//again as text - "5M" reports `in '535M'`, "T5M" reports `in '84T5M'`.  Reported, not fixed here.
				EXPECT_NE( string{e.what()}.find("Expected 'P'"), string::npos ) << e.what();
			}
		}
		EXPECT_NO_THROW( Chrono::ToDuration(sv{"P5M"}) );
	}

	//the nullopt contract is load-bearing: Settings and json durations call only through here, and a throw escaping
	//would take down config parsing over one bad key rather than falling back to the caller's default.
	TEST( ChronoTests, TryToDurationNullopt ){
		EXPECT_FALSE( Chrono::TryToDuration(string{"5M"}).has_value() );
		EXPECT_FALSE( Chrono::TryToDuration(string{""}).has_value() );
		EXPECT_EQ( Chrono::TryToDuration(string{"P4D"}), duration_cast<Duration>(days{4}) );
		//the level argument only re-levels the swallowed exception, which is already logged by then - it changes
		//nothing observable here, so this asserts only that passing one is still a nullopt rather than a throw.
		EXPECT_FALSE( Chrono::TryToDuration(string{"5M"}, ELogLevel::Critical).has_value() );
	}

	//an unknown zone is operator error in a config, not a reason to fail startup: locate_zone throws and the caller's
	//default has to come back instead.  The identity check is the point - returning some other valid zone would read
	//as success at every call site.
	TEST( ChronoTests, ToTimeZoneFallsBack ){
		let& dflt = *std::chrono::current_zone();
		EXPECT_EQ( &Chrono::ToTimeZone("No/Such_Zone", dflt), &dflt );
		EXPECT_EQ( &Chrono::ToTimeZone("", dflt), &dflt );
		//by name, not by address: the spelling is canonicalized ("UTC" comes back as "Etc/UTC"), and the pointers are
		//not comparable across the dll boundary either - Jde.dll and this exe each hold their own tzdb, so
		//locate_zone("UTC") here and inside ToTimeZone return different objects for the same zone.
		EXPECT_EQ( Chrono::ToTimeZone("UTC", dflt).name(), std::chrono::locate_zone("UTC")->name() ) << "a zone that does exist must not fall back";
	}

	//IsoStringRoundTrips only ever round-trips a whole second, so the fractional branch of the parse was never fed a
	//non-zero fraction: a parser that dropped subseconds entirely passed it.
	TEST( ChronoTests, IsoStringRoundTripsSubSecond ){
		let tp = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5, duration_cast<Duration>(milliseconds{123}) );
		EXPECT_EQ( ToIsoString<milliseconds>(tp), "2024-01-02T03:04:05.123Z" );
		EXPECT_EQ( Chrono::ToTimePoint(ToIsoString<milliseconds>(tp)), tp );
		EXPECT_NE( Chrono::ToTimePoint(ToIsoString<seconds>(tp)), tp ) << "the fraction is what distinguishes them - if this holds, it was never parsed";

		let micro = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5, duration_cast<Duration>(microseconds{123456}) );
		EXPECT_EQ( Chrono::ToTimePoint(ToIsoString<microseconds>(micro)), micro );
		//a fraction and a numeric offset in the one string: the fraction is consumed digit by digit, so the offset has to
		//still be found after it rather than inside it.
		EXPECT_EQ( Chrono::ToTimePoint("2024-01-02T03:04:05.123456+05:00"), micro-hours{5} );
	}

	//the local-time renderer used by the console log pattern.  The hour is the machine's zone and not assertable, but
	//the shape and the seconds field are: an offset is a whole number of minutes everywhere, so seconds do not move.
	TEST( ChronoTests, LocalTimeMilliShape ){
		let tp = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5, duration_cast<Duration>(milliseconds{123}) );
		let local = Chrono::LocalTimeMilli( tp );
		ASSERT_GE( local.size(), 8u ) << local;
		EXPECT_EQ( local[2], ':' ) << local;
		EXPECT_EQ( local[5], ':' ) << local;
		EXPECT_EQ( local.substr(6,2), "05" ) << "seconds are zone-invariant: " << local;
		EXPECT_NE( local.find(".123"), string::npos ) << "the milli in LocalTimeMilli: " << local;
	}
}
