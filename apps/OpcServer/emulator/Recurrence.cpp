#include "Recurrence.h"
#include <absl/random/random.h>
#include <jde/fwk/str.h>
#include "Devices.h"

#define let const auto
namespace Jde::Opc::Emulator{
	Ω toSeconds( Duration d )ι->double{ return duration<double>( d ).count(); }
	//06:00 or 06:00:00, on the 24 hour clock.
	Ω timeOfDay( sv text, sv owner, SL sl )ε->Duration{
		let two = [text]( uint i )->optional<uint>{
			let digits = i+1<text.size() && std::isdigit( (unsigned char)text[i] ) && std::isdigit( (unsigned char)text[i+1] );
			return digits ? optional<uint>{ (text[i]-'0')*10+(text[i+1]-'0') } : nullopt;
		};
		let withSeconds = text.size()==8;
		let h = two( 0 ); let m = two( 3 ); let s = withSeconds ? two( 6 ) : optional<uint>{ 0 };
		let valid = ( text.size()==5 || withSeconds ) && text[2]==':' && ( !withSeconds || text[5]==':' ) && h && m && s && *h<24 && *m<60 && *s<60;
		THROW_IFSL( !valid, "{}: `at` is '{}' - it takes a time of day, 06:00 or 06:00:00.", owner, text );
		return hours{ *h }+minutes{ *m }+seconds{ *s };
	}
	Ω shapeKeys( Recurrence::EShape shape )ι->std::span<const sv>{
		static constexpr array<sv,2> once{ "start", "for" }, at{ "at", "for" };
		static constexpr array<sv,3> every{ "start", "every", "for" }, random{ "mean", "min", "max" };
		using EShape = Recurrence::EShape;
		return shape==EShape::Once ? std::span<const sv>{ once } : shape==EShape::Every ? std::span<const sv>{ every } : shape==EShape::Random ? std::span<const sv>{ random } : std::span<const sv>{ at };
	}
	Recurrence::Recurrence( const jobject& o, sv owner, std::initializer_list<sv> own, SL sl )ε{
		let duration = [&]( sv key ){ return FindKey<Duration>( o, key, owner, sl ); };
		let every = duration( "every" ); let mean = duration( "mean" ); let at = FindKey<string>( o, "at", owner, sl );
		THROW_IFSL( (every ? 1 : 0)+(mean ? 1 : 0)+(at ? 1 : 0)>1, "{} takes one shape:  {{start, for}} once, {{start, every, for}}, {{mean, min, max}} or {{at, for}}.", owner );
		Shape = every ? EShape::Every : mean ? EShape::Random : at ? EShape::At : EShape::Once;
		let keys = shapeKeys( Shape );
		for( let& entry : o ){//a slow period pasted into `stops` must not run as a stop, its `factor` unread.
			let key = sv{ entry.key() };
			THROW_IFSL( std::ranges::find(own, key)==own.end() && std::ranges::find(keys, key)==keys.end(), "{}: `{}` is no key of {{{}}} - it takes those and {{{}}}.", owner, key, Str::Join(keys, ", "), Str::Join(own, ", ") );
		}
		if( Shape==EShape::Random ){
			Mean = *mean; Min = duration( "min" ).value_or( Duration::zero() ); Max = duration( "max" ).value_or( Duration::zero() );
			THROW_IFSL( Mean<=Duration::zero() || Min<=Duration::zero(), "{}: `mean` and `min` must be positive.", owner );
			THROW_IFSL( Max<Min, "{}: `max` ({}) must not be below `min` ({}).", owner, Chrono::ToString(Max), Chrono::ToString(Min) );
			return;
		}
		Length = duration( "for" ).value_or( Duration::zero() );
		if( Shape==EShape::At ){
			At = timeOfDay( *at, owner, sl );
			//a longer one would run into the next day's on a day the clocks go forward, and never release.
			THROW_IFSL( Length<=Duration::zero() || Length>=23h, "{}: `for` must be positive and under 23 hours, the shortest day a clock change makes.", owner );
			return;
		}
		let start = duration( "start" );
		THROW_IFSL( start && *start<Duration::zero(), "{}: `start` must not be negative.", owner );
		if( every ){
			Every = *every;
			THROW_IFSL( Length<=Duration::zero() || Every<=Length, "{}: `for` must be positive and shorter than its `every`.", owner );
		}
		else
			THROW_IFSL( Length<=Duration::zero(), "{}: `for` must be positive.", owner );
		Begin = start.value_or( every ? Every : Duration::zero() );
		End = Begin+Length;
	}
	//An `at` window's Begin:  the one under way at the device's clock, else the next.
	Ω nextAt( const Recurrence& r, const Device& device )ι->Duration{
		let now = device.Start+device.Clock;
		for( auto day = floor<days>( device.Zone->to_local(now) )-days{ 1 }; ; day += days{ 1 } ){//from yesterday's, which may still run.
			if( let begin = device.Zone->to_sys(day+r.At, choose::earliest); begin+r.Length>now )
				return begin-device.Start;
		}
	}
	α Recurrence::Advance( const Device& device, std::mt19937_64& engine )ι->void{
		if( Shape==EShape::Once ){
			Begin = End = Duration::max();
			return;
		}
		if( Shape==EShape::Every )
			Begin += Every;
		else if( Shape==EShape::Random ){
			Begin = End+duration_cast<Duration>( duration<double>{absl::Exponential<double>(engine, 1/toSeconds(Mean))} );
			Length = Duration{ absl::Uniform<Duration::rep>(absl::IntervalClosed, engine, Min.count(), Max.count()) };
		}
		else
			Begin = nextAt( *this, device );
		End = Begin+Length;
	}
	α Recurrence::Anchor( Duration drift, const Device& device )ι->void{
		if( Shape!=EShape::At )
			return;
		Begin -= drift; End -= drift;
		if( device.Clock>=End ){//the pause ran past it:  on to the next, as Sample would have.
			Begin = nextAt( *this, device );
			End = Begin+Length;
		}
	}
}