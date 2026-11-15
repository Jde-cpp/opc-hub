#include "Devices.h"
#include <ranges>
#include <absl/random/random.h>
#include <jde/fwk/str.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr sv _status{ "status" }, _reasonCode{ "reasonCode" };//the pumps' names for the two tags a stop moves.

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
		static constexpr array<sv,2> every{ "every", "for" }, at{ "at", "for" };
		static constexpr array<sv,3> random{ "mean", "min", "max" };
		return shape==Recurrence::EShape::Every ? std::span<const sv>{ every } : shape==Recurrence::EShape::Random ? std::span<const sv>{ random } : std::span<const sv>{ at };
	}
	Recurrence::Recurrence( const jobject& o, sv owner, sv own, SL sl )ε{
		let duration = [&]( sv key ){ return FindKey<Duration>( o, key, owner, sl ); };
		let every = duration( "every" ); let mean = duration( "mean" ); let at = FindKey<string>( o, "at", owner, sl );
		THROW_IFSL( (every ? 1 : 0)+(mean ? 1 : 0)+(at ? 1 : 0)!=1, "{} takes one shape:  {{every, for}}, {{mean, min, max}} or {{at, for}}.", owner );
		Shape = every ? EShape::Every : mean ? EShape::Random : EShape::At;
		let keys = shapeKeys( Shape );
		for( let& entry : o ){//a slow period pasted into `stops` must not run as a stop, its `factor` unread.
			let key = sv{ entry.key() };
			THROW_IFSL( key!=own && std::ranges::find(keys, key)==keys.end(), "{}: `{}` is no key of {{{}}} - it takes those and `{}`.", owner, key, Str::Join(keys, ", "), own );
		}
		if( Shape==EShape::Random ){
			Mean = *mean; Min = duration( "min" ).value_or( Duration::zero() ); Max = duration( "max" ).value_or( Duration::zero() );
			THROW_IFSL( Mean<=Duration::zero() || Min<=Duration::zero(), "{}: `mean` and `min` must be positive.", owner );
			THROW_IFSL( Max<Min, "{}: `max` ({}) must not be below `min` ({}).", owner, Chrono::ToString(Max), Chrono::ToString(Min) );
			return;
		}
		Length = duration( "for" ).value_or( Duration::zero() );
		if( every ){
			Every = *every;
			THROW_IFSL( Length<=Duration::zero() || Every<=Length, "{}: `for` must be positive and shorter than its `every`.", owner );
		}
		else{
			At = timeOfDay( *at, owner, sl );
			//a longer one would run into the next day's on a day the clocks go forward, and never release.
			THROW_IFSL( Length<=Duration::zero() || Length>=23h, "{}: `for` must be positive and under 23 hours, the shortest day a clock change makes.", owner );
		}
	}
	α Recurrence::Advance( Device& device )ι->void{
		if( Shape==EShape::Every )
			Begin += Every;
		else if( Shape==EShape::Random ){
			Begin = End+duration_cast<Duration>( duration<double>{absl::Exponential<double>(device.Engine, 1/toSeconds(Mean))} );
			Length = Duration{ absl::Uniform<Duration::rep>(absl::IntervalClosed, device.Engine, Min.count(), Max.count()) };
		}
		else{
			let now = device.Start+device.Clock;
			for( auto day = floor<days>( device.Zone->to_local(now) )-days{ 1 }; ; day += days{ 1 } ){//from yesterday's, which may still run.
				if( let begin = device.Zone->to_sys(day+At, choose::earliest); begin+Length>now ){
					Begin = begin-device.Start;
					break;
				}
			}
		}
		End = Begin+Length;
	}
	Stop::Stop( const jobject& o, sv device, SL sl )ε:
		Recurrence{ o, Ƒ("Device '{}' stop", device), "code", sl },
		Code{ FindKey<UA_Int32>(o, "code", Ƒ("Device '{}' stop", device), sl).value_or(0) }
	{}
	Slow::Slow( const jobject& o, sv device, SL sl )ε:
		Recurrence{ o, Ƒ("Device '{}' slow period", device), "factor", sl },
		Factor{ FindKey<double>(o, "factor", Ƒ("Device '{}' slow period", device), sl).value_or(0) }{
		THROW_IFSL( !(Factor>1), "Device '{}': a slow period's `factor` must exceed 1 - the cycle takes that many times as long.", device );
	}

	//A generated `status`:  what Sample made of it, its own generator moved to Device::StatusSource.
	struct DeviceOn final : IGenerator{
		DeviceOn( const Device& device )ι:_device{ device }{}
		α Next( Duration, double )ι->double override{ return _device.On ? 1 : 0; }
		const Device& _device;
	};
	//`reasonCode`:  the active stop's code.
	struct StopCode final : IGenerator{
		StopCode( const Device& device )ι:_device{ device }{}
		α Next( Duration, double )ι->double override{ return _device.Code; }
		const Device& _device;
	};

	α Device::Sample( Duration dt )ι->void{
		let active = [this]( let& entries ){ return std::ranges::find_if( entries, [this](let& x){ return x.IsActive(Clock); } ); };
		//At its End, in the lists' order:  a seed draws the same windows whatever the periods.
		let advance = [this]{
			for( auto& stop : Stops ){
				if( Clock>=stop.End )
					stop.Advance( *this );
			}
			for( auto& slow : Slows ){
				if( Clock>=slow.End )
					slow.Advance( *this );
			}
		};
		let period = dt;
		double rate{};
		do{
			auto piece = dt;
			for( let& stop : Stops )
				piece = std::min( piece, stop.NextEdge(Clock)-Clock );
			for( let& slow : Slows )
				piece = std::min( piece, slow.NextEdge(Clock)-Clock );
			let commanded = StatusSource ? StatusSource->Next( piece, Command ? 1 : 0 )!=0 : Command;
			let slow = active( Slows );
			rate = commanded && active( Stops )==Stops.end() ? 1/( slow==Slows.end() ? 1 : slow->Factor ) : 0;//through the piece, which no edge cuts.
			Clock += piece;
			dt -= piece;
			advance();
			let coded = std::ranges::find_if( Stops, [this](let& x){ return x.Code && x.IsActive(Clock); } );//an uncoded stop never hides a coded one's reason.
			Code = coded==Stops.end() ? 0 : coded->Code;
			On = commanded && active( Stops )==Stops.end();//at the piece's end:  what `status` publishes.
			for( auto& tag : Tags ){
				if( tag.Generator && !tag.Generator->PerPeriod() )//sampled even through a fault that holds the reading: the process runs on, the sensor is blind.
					tag.Status = tag.Quality.Apply( piece, tag.Generator->Next(piece, rate), tag.Value );
			}
		}while( dt>Duration::zero() );
		for( auto& tag : Tags ){
			if( tag.Generator && tag.Generator->PerPeriod() )
				tag.Status = tag.Quality.Apply( period, tag.Generator->Next(period, rate), tag.Value );
		}
	}
	α Device::Anchor( TimePoint wall )ι->void{
		let drift = wall-( Start+Clock );
		Start += drift;
		let shift = [&]( auto& entries ){
			for( auto& entry : entries ){
				if( entry.Shape!=Recurrence::EShape::At )
					continue;
				entry.Begin -= drift; entry.End -= drift;
				if( Clock>=entry.End )//the pause ran past it:  on to the next, as Sample would have.
					entry.Advance( *this );
			}
		};
		shift( Stops ); shift( Slows );
	}
}
namespace Jde::Opc{
	α Emulator::ParseDevices( const jarray& devices, const FindField& findField, const std::chrono::time_zone* zone, TimePoint start, SL sl )ε->vector<Device>{
		vector<Device> y;
		y.reserve( devices.size() );//never grown after:  Owner and the `status` and `reasonCode` generators point into it.
		for( let& d : devices ){
			let& o = Json::AsObject( d, sl );
			auto& device = y.emplace_back( Json::AsString(o, "path", sl) );
			let lastSegment = device.Path.substr( device.Path.rfind('/')+1 );
			device.Name = lastSegment.substr( lastSegment.rfind('~')+1 );
			device.Start = start;
			let seed = FindKey<uint64_t>( o, "seed", Ƒ("Device '{}'", device.Path), sl );
			let drawn = seed.value_or( (uint64_t)start.time_since_epoch().count()+y.size() );
			device.Engine.seed( drawn );
			auto add = [&]( TagSpec&& spec, up<IGenerator> generator, const UA_DataType* type ){
				Tag tag{ move(spec), move(generator) };
				if( tag.Generator ){
					tag.Type = type ? type : tag.Spec.IsBool() ? &UA_TYPES[UA_TYPES_BOOLEAN] : &UA_TYPES[UA_TYPES_DOUBLE];
					tag.Quality = TagQuality{ tag.Spec.Quality, tag.Spec.SensorMin, tag.Spec.SensorMax };
					if( findField )
						tag.Field = findField( Ƒ("{}.{}", device.Name, tag.Spec.Name) );
				}
				tag.Owner = &device;
				device.Tags.push_back( move(tag) );
			};
			vector<TagSpec> specs;
			for( let& t : Json::AsArray(o, "tags", sl) )
				specs.emplace_back( Json::AsObject(t, sl), sl );
			for( auto& spec : specs ){
				if( spec.Follows.empty() )
					continue;
				let production = std::ranges::find_if( specs, [&](let& s){ return s.Mode==EMode::Production && s.Name==spec.Follows; } );
				THROW_IFSL( production==specs.end(), "Device '{}': tag `{}` follows `{}`, which is not a production tag of the device.", device.Path, spec.Name, spec.Follows );
				spec.RatedRpm = 60/toSeconds( production->Cycle );//units a minute.
			}
			for( auto& spec : specs ){
				if( spec.Name==_status && spec.IsBool() )
					device.StatusTag = device.Tags.size();
				if( spec.Mode==EMode::Production ){
					auto production = MakeProduction( spec );
					auto rejects = spec;
					add( move(spec), move(production.Count), &UA_TYPES[UA_TYPES_UINT32] );
					if( rejects.Reject>0 ){//one counter:  the count's faults are its rejects' too, its range the count's alone.
						rejects.Name = rejects.RejectTag;
						rejects.SensorMin = rejects.SensorMax = nullopt;
						add( move(rejects), move(production.Rejects), &UA_TYPES[UA_TYPES_UINT32] );
					}
				}
				else{
					auto generator = spec.Mode==EMode::Command ? nullptr : MakeGenerator( spec );
					add( move(spec), move(generator), nullptr );
				}
			}
			THROW_IFSL( device.Tags.empty(), "Device '{}' has no tags.", device.Path );
			let generated = device.StatusTag && device.Tags[*device.StatusTag].Generator;
			if( let stops = o.if_contains("stops"); stops ){
				THROW_IFSL( !stops->is_array(), "Device '{}': stops must be an array of {{every, for, code}}, {{mean, min, max, code}} or {{at, for, code}}.", device.Path );
				for( let& stop : stops->get_array() )
					device.Stops.emplace_back( Json::AsObject(stop, sl), device.Path, sl );
			}
			if( let slow = o.if_contains("slow"); slow ){
				THROW_IFSL( !slow->is_array(), "Device '{}': slow must be an array of the stops' shapes, each with a `factor` where a stop has its `code`.", device.Path );
				for( let& entry : slow->get_array() )
					device.Slows.emplace_back( Json::AsObject(entry, sl), device.Path, sl );
			}
			THROW_IFSL( device.Slows.size() && std::ranges::none_of(device.Tags, [](let& t){ return t.Spec.Mode==EMode::Production; }), "Device '{}': slow periods stretch a production tag's cycle, and it has none.", device.Path );
			let any = [&]( Recurrence::EShape shape ){
				let is = [shape]( let& x ){ return x.Shape==shape; };
				return std::ranges::any_of( device.Stops, is ) || std::ranges::any_of( device.Slows, is );
			};
			if( any(Recurrence::EShape::At) )
				device.Zone = zone ? zone : &Chrono::ToTimeZone( "UTC", "/emulator/zone, UTC where left out,", sl );
			for( auto& stop : device.Stops )//to each first window, in the lists' order, as Sample moves them on.
				stop.Advance( device );
			for( auto& slow : device.Slows )
				slow.Advance( device );
			if( !seed && any(Recurrence::EShape::Random) )
				INFO( "[{}]seed={} - set it as the device's `seed` to draw these random stops again.", device.Name, drawn );
			if( device.Stops.size() ){
				//A command `status` is the UI's:  the emulator subscribes it and never writes it, so it has nothing to hold off.
				THROW_IFSL( !generated, "Device '{}': stops hold a generated `{}` off - a toggle, not a command.", device.Path, _status );
				device.ReasonTag = device.Tags.size();
				add( TagSpec{jobject{{"name", _reasonCode}}, sl}, mu<StopCode>(device), &UA_TYPES[UA_TYPES_INT32] );
			}
			let command = std::ranges::find_if( device.Tags, [](let& t){ return t.Spec.Mode==EMode::Command; } );
			if( generated ){
				THROW_IFSL( command!=device.Tags.end(), "Device '{}': its `{}` is generated, so command tag `{}` would command nothing.", device.Path, _status, command->Spec.Name );
				device.StatusSource = std::exchange( device.Tags[*device.StatusTag].Generator, mu<DeviceOn>(device) );
			}
			device.Command = command==device.Tags.end();
			for( uint i=1; i<device.Tags.size(); ++i ){
				let& name = device.Tags[i].Spec.Name;
				THROW_IFSL( std::ranges::any_of(device.Tags | std::views::take(i), [&](let& tag){ return tag.Spec.Name==name; }), "Device '{}' has two tags named `{}` - a production tag's `rejectTag` (`rejectCount` unless it says) and a stops list's `{}` are tags too.", device.Path, name, _reasonCode );
			}
		}
		THROW_IFSL( y.empty(), "/emulator/devices is empty." );
		for( let& device : y ){
			vector<string> routes;
			for( let& tag : device.Tags ){
				let quality = tag.Spec.Quality.size() ? Ƒ( " quality={}", Str::Join(tag.Spec.Quality | std::views::transform([](let& w){ return ToString(w.Status); }), "/") ) : string{};
				let mode = device.ReasonTag && &tag==&device.Tags[*device.ReasonTag] ? "stops" : ToString( tag.Spec.Mode );
				routes.push_back( Ƒ("{}={}{}{}", tag.Spec.Name, mode, tag.Spec.Mode==EMode::Command ? " (subscribed)" : tag.Field ? " (published)" : " (written)", quality) );
			}
			INFO( "[{}]{}", device.Name, Str::Join(routes, ", ") );
		}
		return y;
	}
}