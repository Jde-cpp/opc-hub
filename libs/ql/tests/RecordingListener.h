#pragma once
//The listener double the subscription tests share:  records every notification it is handed (Changes, Payload) and can be told to
//refuse one (Throws).  A test that only needs a registered listener ignores the recording.
#include <jde/ql/LocalSubscriptions.h>

namespace Jde::QL::Tests{
	struct RecordingListener final : IListener{
		RecordingListener( str name )ι:IListener{name}{}
		α OnChange( const jvalue& j, SubscriptionId clientId )ε->void override{
			Changes.emplace_back( clientId, j );
			if( Throws )
				throw Exception{ Ƒ("{} refuses", Name) };
		}
		α Payload( uint index )Ι->const jobject&{ return Changes.at(index).second.as_object().at("status").as_object(); }//the `status` tests' notification body.
		α Body( uint index )Ε->const jobject&{ //the body under whatever table it is keyed by - {"items":{…}} (the op tests').
			const auto& o = Changes.at( index ).second.as_object();
			THROW_IF( o.empty(), "notification {} is empty.", index );
			return o.begin()->value().as_object();
		}

		vector<std::pair<SubscriptionId,jvalue>> Changes;
		bool Throws{};
	};
}