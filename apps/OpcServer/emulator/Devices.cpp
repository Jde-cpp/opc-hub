#include "Devices.h"
#include <ranges>
#include <jde/fwk/str.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };

	α ParseDevices( const jarray& devices, const FindField& findField, SL sl )ε->vector<Device>{
		vector<Device> y;
		for( let& d : devices ){
			let& o = Json::AsObject( d, sl );
			Device device{ Json::AsString(o, "path", sl) };
			let lastSegment = device.Path.substr( device.Path.rfind('/')+1 );
			device.Name = lastSegment.substr( lastSegment.rfind('~')+1 );
			for( let& t : Json::AsArray(o, "tags", sl) ){
				Tag tag{ TagSpec{Json::AsObject(t, sl), sl} };
				if( tag.Spec.Mode!=EMode::Command ){
					tag.Generator = MakeGenerator( tag.Spec );
					tag.Quality = TagQuality{ tag.Spec.Quality, tag.Spec.SensorMin, tag.Spec.SensorMax };
					if( findField )
						tag.Field = findField( Ƒ("{}.{}", device.Name, tag.Spec.Name) );
				}
				device.Tags.push_back( move(tag) );
			}
			THROW_IFSL( device.Tags.empty(), "Device '{}' has no tags.", device.Path );
			y.push_back( move(device) );
		}
		THROW_IFSL( y.empty(), "/emulator/devices is empty." );
		for( auto& device : y ){//after every push_back: the vectors do not move again.
			vector<string> routes;
			for( auto& tag : device.Tags ){
				tag.Owner = &device;
				let quality = tag.Spec.Quality.size() ? Ƒ( " quality={}", Str::Join(tag.Spec.Quality | std::views::transform([](let& w){ return ToString(w.Status); }), "/") ) : string{};
				routes.push_back( Ƒ("{}={}{}{}", tag.Spec.Name, ToString(tag.Spec.Mode), tag.Spec.Mode==EMode::Command ? " (subscribed)" : tag.Field ? " (published)" : " (written)", quality) );
			}
			INFO( "[{}]{}", device.Name, Str::Join(routes, ", ") );
		}
		return y;
	}
}
