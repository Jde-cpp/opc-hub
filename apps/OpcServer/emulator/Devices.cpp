#include "Devices.h"
#include <ranges>
#include <jde/fwk/str.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr sv _status{ "status" }, _reasonCode{ "reasonCode" };//the pumps' names for the two tags a stop moves.

	Stop::Stop( const jobject& o, sv device, SL sl )ε:
		Every{ Json::FindDuration(o, "every").value_or(Duration::zero()) },
		For{ Json::FindDuration(o, "for").value_or(Duration::zero()) },
		Code{ Json::FindNumber<UA_Int32>(o, "code").value_or(0) }{
		THROW_IFSL( For<=Duration::zero() || Every<=For, "Device '{}': a stop's `for` must be positive and shorter than its `every`.", device );
		THROW_IFSL( !Code, "Device '{}': a stop needs a `code` other than 0, which is no reason.", device );
	}

	//`reasonCode`:  Device::Sample writes the active stop's code over it.
	struct StopCode final : IGenerator{
		α Next( Duration, bool )ι->double override{ return 0; }
	};

	α Device::Sample( Duration dt )ι->void{
		Clock += dt;
		let stop = std::ranges::find_if( Stops, [this](let& s){ return s.IsActive(Clock); } );
		Code = stop==Stops.end() ? 0 : stop->Code;
		let status = StatusTag ? &Tags[*StatusTag] : nullptr;
		let reason = ReasonTag ? &Tags[*ReasonTag] : nullptr;
		//A generated status is sampled through a stop:  its own clock runs on, as a held reading's does.
		let commanded = status && status->Generator ? status->Generator->Next( dt, Command )!=0 : Command;
		On = commanded && stop==Stops.end();
		for( auto& tag : Tags ){
			if( !tag.Generator )
				continue;
			let raw = &tag==status ? (double)On : &tag==reason ? (double)Code : tag.Generator->Next( dt, On );
			//The generator is sampled even through a fault that holds the reading: the process runs on, the sensor is blind.
			tag.Status = tag.Quality.Apply( dt, raw, tag.Value );
		}
	}

	α ParseDevices( const jarray& devices, const FindField& findField, SL sl )ε->vector<Device>{
		vector<Device> y;
		for( let& d : devices ){
			let& o = Json::AsObject( d, sl );
			Device device{ Json::AsString(o, "path", sl) };
			let lastSegment = device.Path.substr( device.Path.rfind('/')+1 );
			device.Name = lastSegment.substr( lastSegment.rfind('~')+1 );
			auto add = [&]( TagSpec&& spec, up<IGenerator> generator, const UA_DataType* type ){
				Tag tag{ move(spec), move(generator) };
				tag.Type = type;
				if( tag.Generator ){
					tag.Quality = TagQuality{ tag.Spec.Quality, tag.Spec.SensorMin, tag.Spec.SensorMax };
					if( findField )
						tag.Field = findField( Ƒ("{}.{}", device.Name, tag.Spec.Name) );
				}
				device.Tags.push_back( move(tag) );
			};
			for( let& t : Json::AsArray(o, "tags", sl) ){
				TagSpec spec{ Json::AsObject(t, sl), sl };
				THROW_IFSL( spec.Name==_reasonCode && o.contains("stops"), "Device '{}': its stops write `{}` - it is not a tag of its own.", device.Path, _reasonCode );
				if( spec.Name==_status && spec.IsBool() )
					device.StatusTag = device.Tags.size();
				if( spec.Mode==EMode::Production ){
					auto production = MakeProduction( spec );
					auto rejects = spec;
					add( move(spec), move(production.Count), &UA_TYPES[UA_TYPES_UINT32] );
					if( rejects.Reject>0 ){//the count's own schedule and range stay the count's.
						rejects.Name = rejects.RejectTag;
						rejects.Quality.clear();
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
			if( let stops = o.if_contains("stops"); stops ){
				THROW_IFSL( !stops->is_array(), "Device '{}': stops must be an array of {{every, for, code}}.", device.Path );
				for( let& stop : stops->get_array() )
					device.Stops.emplace_back( Json::AsObject(stop, sl), device.Path, sl );
			}
			if( device.Stops.size() ){
				//A command `status` is the UI's:  the emulator subscribes it and never writes it, so it has nothing to hold off.
				THROW_IFSL( !device.StatusTag || !device.Tags[*device.StatusTag].Generator, "Device '{}': stops hold a generated `{}` off - a toggle, not a command.", device.Path, _status );
				device.ReasonTag = device.Tags.size();
				add( TagSpec{jobject{{"name", _reasonCode}}, sl}, mu<StopCode>(), &UA_TYPES[UA_TYPES_INT32] );
			}
			y.push_back( move(device) );
		}
		THROW_IFSL( y.empty(), "/emulator/devices is empty." );
		for( auto& device : y ){//after every push_back: the vectors do not move again.
			vector<string> routes;
			for( auto& tag : device.Tags ){
				tag.Owner = &device;
				let quality = tag.Spec.Quality.size() ? Ƒ( " quality={}", Str::Join(tag.Spec.Quality | std::views::transform([](let& w){ return ToString(w.Status); }), "/") ) : string{};
				let mode = device.ReasonTag && &tag==&device.Tags[*device.ReasonTag] ? "stops" : ToString( tag.Spec.Mode );
				routes.push_back( Ƒ("{}={}{}{}", tag.Spec.Name, mode, tag.Spec.Mode==EMode::Command ? " (subscribed)" : tag.Field ? " (published)" : " (written)", quality) );
			}
			INFO( "[{}]{}", device.Name, Str::Join(routes, ", ") );
		}
		return y;
	}
}