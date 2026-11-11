#include "Devices.h"
#include <ranges>
#include <jde/fwk/str.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr sv _status{ "status" }, _reasonCode{ "reasonCode" };//the pumps' names for the two tags a stop moves.

	Ω stopSchedule( const jobject& o, sv owner, SL sl )ε->Schedule{
		let every = FindKey<Duration>( o, "every", owner, sl ).value_or( Duration::zero() );
		return { every, FindKey<Duration>(o, "for", owner, sl).value_or(Duration::zero()), every };
	}
	Stop::Stop( const jobject& o, sv device, SL sl )ε:
		Schedule{ stopSchedule(o, Ƒ("Device '{}' stop", device), sl) },
		Code{ FindKey<UA_Int32>(o, "code", Ƒ("Device '{}' stop", device), sl).value_or(0) }{
		THROW_IFSL( Length<=Duration::zero() || *Every<=Length, "Device '{}': a stop's `for` must be positive and shorter than its `every`.", device );
		THROW_IFSL( !Code, "Device '{}': a stop needs a `code` other than 0, which is no reason.", device );
	}

	//A generated `status`:  what Sample made of it, its own generator moved to Device::StatusSource.
	struct DeviceOn final : IGenerator{
		DeviceOn( const Device& device )ι:_device{ device }{}
		α Next( Duration, bool )ι->double override{ return _device.On ? 1 : 0; }
		const Device& _device;
	};
	//`reasonCode`:  the active stop's code.
	struct StopCode final : IGenerator{
		StopCode( const Device& device )ι:_device{ device }{}
		α Next( Duration, bool )ι->double override{ return _device.Code; }
		const Device& _device;
	};

	α Device::Sample( Duration dt )ι->void{
		let stopAt = [this]( Duration t ){ return std::ranges::find_if( Stops, [t](let& s){ return s.IsActive(t); } ); };
		do{
			auto piece = dt;
			for( let& stop : Stops )
				piece = std::min( piece, *stop.NextEdge(Clock)-Clock );//a stop repeats, so it always has a next edge.
			let commanded = StatusSource ? StatusSource->Next( piece, Command )!=0 : Command;
			let ran = commanded && stopAt( Clock )==Stops.end();//through the piece, which no edge cuts.
			Clock += piece;
			dt -= piece;
			let stop = stopAt( Clock );
			Code = stop==Stops.end() ? 0 : stop->Code;
			On = commanded && stop==Stops.end();//at the piece's end:  what `status` publishes.
			for( auto& tag : Tags ){
				if( tag.Generator )//sampled even through a fault that holds the reading: the process runs on, the sensor is blind.
					tag.Status = tag.Quality.Apply( piece, tag.Generator->Next(piece, ran), tag.Value );
			}
		}while( dt>Duration::zero() );
	}
}
namespace Jde::Opc{
	α Emulator::ParseDevices( const jarray& devices, const FindField& findField, SL sl )ε->vector<Device>{
		vector<Device> y;
		y.reserve( devices.size() );//never grown after:  Owner and the `status` and `reasonCode` generators point into it.
		for( let& d : devices ){
			let& o = Json::AsObject( d, sl );
			auto& device = y.emplace_back( Json::AsString(o, "path", sl) );
			let lastSegment = device.Path.substr( device.Path.rfind('/')+1 );
			device.Name = lastSegment.substr( lastSegment.rfind('~')+1 );
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
			for( let& t : Json::AsArray(o, "tags", sl) ){
				TagSpec spec{ Json::AsObject(t, sl), sl };
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
				THROW_IFSL( !stops->is_array(), "Device '{}': stops must be an array of {{every, for, code}}.", device.Path );
				for( let& stop : stops->get_array() )
					device.Stops.emplace_back( Json::AsObject(stop, sl), device.Path, sl );
			}
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