#include "helpers.h"

//Buckets by shift, day, week, month and duration, across a month boundary and the clock-change nights.
namespace Jde::Calendar::Tests{
	Ω labels( const vector<Bucket>& buckets )ι->vector<string>{
		vector<string> y;
		for( let& b : buckets )
			y.push_back( b.Label );
		return y;
	}

	TEST( BucketTests, ByShift ){
		let plant = Plant();
		let day = plant.Buckets( Utc(2026,7,1,10), Utc(2026,7,2,10), EBucket::Shift );
		ASSERT_EQ( labels(day), (vector<string>{"Day", "Evening", "Night"}) );
		EXPECT_TP( day[0].Start, Utc(2026,7,1,10) );
		EXPECT_TP( day[0].End, Utc(2026,7,1,18) );
		EXPECT_TP( day[2].End, Utc(2026,7,2,10) );
		let clipped = plant.Buckets( Utc(2026,7,1,12), Utc(2026,7,2,4), EBucket::Shift );
		ASSERT_EQ( clipped.size(), 3u );
		EXPECT_TP( clipped[0].Start, Utc(2026,7,1,12) ) << "the first clipped to the range";
		EXPECT_TP( clipped[2].Start, Utc(2026,7,2,2) ) << "Night, 22:00 EDT";
		EXPECT_TP( clipped[2].End, Utc(2026,7,2,4) ) << "and the last clipped";
		EXPECT_TRUE( plant.Buckets(Utc(2026,7,4,12), Utc(2026,7,5,12), EBucket::Shift).empty() ) << "the weekend falls in no bucket";
		let open = Plant( jarray{ ExceptionRow(1, 1, "Overtime", Utc(2026,7,11,12), Utc(2026,7,11,16), true) } );
		EXPECT_EQ( labels(open.Buckets(Utc(2026,7,11,10), Utc(2026,7,12,10), EBucket::Shift)), (vector<string>{"Overtime"}) );
	}

	TEST( BucketTests, ByDayAcrossAMonth ){
		let plant = Plant();
		let days = plant.Buckets( Utc(2026,6,29,12), Utc(2026,7,2,12), EBucket::Day );
		ASSERT_EQ( labels(days), (vector<string>{"2026-06-29", "2026-06-30", "2026-07-01", "2026-07-02"}) );
		EXPECT_TP( days[0].Start, Utc(2026,6,29,12) ) << "clipped";
		EXPECT_TP( days[0].End, Utc(2026,6,30,10) ) << "the production day ends at 06:00";
		EXPECT_TP( days[2].Start, Utc(2026,7,1,10) );
		EXPECT_TP( days[3].End, Utc(2026,7,2,12) );
		EXPECT_EQ( plant.Buckets(Utc(2026,7,1,10), Utc(2026,7,2,10), EBucket::Day).size(), 1u ) << "exactly one production day";
	}

	TEST( BucketTests, ByDayAcrossTheClockChanges ){
		let plant = Plant();
		let spring = plant.Buckets( Utc(2026,3,7,11), Utc(2026,3,9,10), EBucket::Day );
		ASSERT_EQ( labels(spring), (vector<string>{"2026-03-07", "2026-03-08"}) );
		EXPECT_EQ( spring[0].End-spring[0].Start, 23h );
		EXPECT_TP( spring[0].End, Utc(2026,3,8,10) );
		EXPECT_EQ( spring[1].End-spring[1].Start, 24h );
		let fall = plant.Buckets( Utc(2026,10,31,10), Utc(2026,11,2,11), EBucket::Day );
		ASSERT_EQ( labels(fall), (vector<string>{"2026-10-31", "2026-11-01"}) );
		EXPECT_EQ( fall[0].End-fall[0].Start, 25h );
		EXPECT_TP( fall[0].End, Utc(2026,11,1,11) );
		EXPECT_EQ( fall[1].End-fall[1].Start, 24h );
	}

	TEST( BucketTests, ByWeek ){
		let plant = Plant();
		let weeks = plant.Buckets( Utc(2026,6,29,10), Utc(2026,7,13,10), EBucket::Week );
		ASSERT_EQ( labels(weeks), (vector<string>{"2026-W27", "2026-W28"}) );
		EXPECT_TP( weeks[0].Start, Utc(2026,6,29,10) ) << "Monday's day start";
		EXPECT_TP( weeks[0].End, Utc(2026,7,6,10) );
		EXPECT_TP( weeks[1].End, Utc(2026,7,13,10) );
		let midweek = plant.Buckets( Utc(2026,7,1,12), Utc(2026,7,8,12), EBucket::Week );
		ASSERT_EQ( labels(midweek), (vector<string>{"2026-W27", "2026-W28"}) );
		EXPECT_TP( midweek[0].Start, Utc(2026,7,1,12) ) << "clipped";
		EXPECT_TP( midweek[0].End, Utc(2026,7,6,10) ) << "the week turns at Monday's day start";
		EXPECT_TP( midweek[1].End, Utc(2026,7,8,12) );
		let sunday = plant.Buckets( Utc(2026,7,5,12), Utc(2026,7,6,12), EBucket::Week );
		ASSERT_EQ( labels(sunday), (vector<string>{"2026-W27", "2026-W28"}) ) << "Sunday is the week's last day";
	}

	TEST( BucketTests, ByMonth ){
		let plant = Plant();
		let months = plant.Buckets( Utc(2026,6,15,10), Utc(2026,7,15,10), EBucket::Month );
		ASSERT_EQ( labels(months), (vector<string>{"2026-06", "2026-07"}) );
		EXPECT_TP( months[0].Start, Utc(2026,6,15,10) );
		EXPECT_TP( months[0].End, Utc(2026,7,1,10) ) << "the month turns at the 1st's day start";
		EXPECT_TP( months[1].End, Utc(2026,7,15,10) );
		let fall = plant.Buckets( Utc(2026,10,15,10), Utc(2026,11,15,11), EBucket::Month );
		ASSERT_EQ( labels(fall), (vector<string>{"2026-10", "2026-11"}) );
		EXPECT_TP( fall[0].End, Utc(2026,11,1,11) ) << "November's first day starts at 06:00 EST";
		let yearEnd = plant.Buckets( Utc(2026,12,31,12), Utc(2027,1,1,12), EBucket::Month );
		EXPECT_EQ( labels(yearEnd), (vector<string>{"2026-12", "2027-01"}) );
	}

	TEST( BucketTests, ByDuration ){
		let plant = Plant();
		let hours = plant.Buckets( Utc(2026,3,8,6), Utc(2026,3,8,9), ParseBucketBy("PT1H") );
		ASSERT_EQ( labels(hours), (vector<string>{"2026-03-08T06:00:00Z", "2026-03-08T07:00:00Z", "2026-03-08T08:00:00Z"}) ) << "from the range's start, whatever the clocks do";
		EXPECT_TP( hours[2].End, Utc(2026,3,8,9) );
		let partial = plant.Buckets( Utc(2026,7,1,10), Utc(2026,7,1,10,45), ParseBucketBy("PT30M") );
		ASSERT_EQ( partial.size(), 2u );
		EXPECT_TP( partial[1].End, Utc(2026,7,1,10,45) ) << "the last clipped";
		EXPECT_TRUE( std::holds_alternative<EBucket>(ParseBucketBy("Week")) );
		EXPECT_EQ( std::get<EBucket>(ParseBucketBy("month")), EBucket::Month );
		EXPECT_EQ( std::get<Duration>(ParseBucketBy("PT1M")), 1min );
	}

	TEST( BucketTests, ByDurationPastTheClock ){
		let plant = Plant();
		let pastTheClock = Duration::max()-Utc(2026,7,1).time_since_epoch()+hours{24};//start+by overflows, on any tick.
		let one = plant.Buckets( Utc(2026,7,1), Utc(2026,7,2), pastTheClock );
		ASSERT_EQ( labels(one), (vector<string>{"2026-07-01T00:00:00Z"}) ) << "one bucket, not a wrapped start";
		EXPECT_TP( one[0].End, Utc(2026,7,2) );
		let exact = plant.Buckets( Utc(2026,7,1), Utc(2026,7,1,2), ParseBucketBy("PT1H") );
		EXPECT_EQ( exact.size(), 2u ) << "a last bucket that ends on the range's end";
	}

	TEST( BucketTests, ByRefused ){
		EXPECT_NE( Refused([]{ ParseBucketBy("fortnight"); }).find("not shift, day, week, month or an ISO 8601 duration"), string::npos );
		EXPECT_NE( Refused([]{ ParseBucketBy("PT30S"); }).find("under a minute"), string::npos );
		EXPECT_NE( Refused([]{ Plant().Buckets(Utc(2026,7,1), Utc(2026,7,2), seconds{30}); }).find("under a minute"), string::npos );
		EXPECT_NE( Refused([]{ Plant().Buckets(Utc(2026,7,1), Utc(2026,7,1), seconds{30}); }).find("under a minute"), string::npos ) << "an empty range too";
	}
}