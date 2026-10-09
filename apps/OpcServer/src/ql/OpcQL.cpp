#include "OpcQL.h"
#include <jde/app/client/IAppClient.h>
#include <jde/access/AccessException.h>
#include "../globals.h"

namespace Jde::Opc{
	sp<Server::OpcQL> _ql;
	α Server::QLPtr()ι->sp<OpcQL>{ ASSERT(_ql); return _ql; }
	α Server::QL()ι->OpcQL&{ return *QLPtr(); }
	α Server::Schemas()ι->const vector<sp<DB::AppSchema>>&{ return QL().Schemas(); }
	α Server::ConfigureQL( sp<DB::AppSchema> schema, sp<Access::Authorize> authorizer )ι->void{
		QL::Configure( {schema} );
		_ql = ms<OpcQL>( move(schema), authorizer );
	}
}

namespace Jde::Opc::Server{
	OpcQL::OpcQL( sp<DB::AppSchema>&& schema, sp<Access::Authorize> authorizer )ι:
		App::AppQL{ {move(schema)}, move(authorizer) }{
		//QL::Configure is done once by ConfigureQL (the only construction site) before this runs; calling it here again was redundant.
	}

	α OpcQL::CustomQuery( QL::TableQL&, QL::Creds, SL )ι->up<TAwait<jvalue>>{return nullptr;}
	α OpcQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		const auto executer = creds.UserPK();
		//the app server pushes updateLogSetting here when its instance_tag_levels rows change; without this route the push comes back an error.
		if( auto await = LogSettingsMutation(m, creds, AppClient(), sl); await )
			return await;
		//The opc schema owns no tables at all now (its address space is NodeSet2 xml), so nothing below has a resource to gate
		//on and Authorize::Test would read every name as "not enabled - allow".  The schema sync's .mutation files (SchemaDdl,
		//UserPK::System) are the only ql writer;  everything else is a web request.
		return executer.Value==UserPK::System
			? nullptr
			: up<TAwait<jvalue>>{ mu<App::RefusedMutation>(Ƒ("'{}' is not supported;  the OpcServer's schema owns no tables to write.", m.CommandName), executer, EHttpStatus::Forbidden, sl) };
	}

}