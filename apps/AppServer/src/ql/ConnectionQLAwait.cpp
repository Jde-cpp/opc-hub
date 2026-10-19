#include "ConnectionQLAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/ql/QLAwait.h>
#include "../LocalClient.h"
#include "../WebServer.h"

#define let const auto

namespace Jde::App::Server{
	//{ connections{id programName instanceName hostName created status{memory values}} }
	α ConnectionQLAwait::IsApplicable( QL::TableQL& q )ι->bool{
		return q.JsonName.starts_with( "connection" ) && q.FindTable( "status" );
	}

	α ConnectionQLAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			flat_map<ConnectionPK, jvalue> conStatuses;
			if( auto status = _query.ExtractTable("status"); status ){
				auto sessions = Server::QuerySessions( move(*status), _creds.UserPK(), _sl );//by reference:  its mutex makes it immovable.
				conStatuses = co_await Any( sessions );
				conStatuses.emplace( AppClient()->ConnectionPK(), IApp::Status() );
			}
			bool addedConId = _query.AddColumn( "id" );
			_query.ReturnRaw = true;
			auto connections = co_await QL::QLAwait{ move(_query), _creds, QLPtr(), _sl };
			Json::Visit( connections, [addedConId,&conStatuses](jobject& o){
				let connectionPK = QL::AsId<ConnectionPK>( o );
				if( addedConId )
					o.erase( "id" );
				if( auto it=conStatuses.find(connectionPK); it!=conStatuses.end() )
					o["status"] = move( it->second );
			});
			Resume( move(connections) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}