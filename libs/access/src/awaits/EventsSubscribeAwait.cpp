#include <jde/access/awaits/EventsSubscribeAwait.h>
#include <jde/db/names.h>
#include <jde/ql/IQL.h>
#include <jde/access/AccessListener.h>
#include "../accessInternal.h"


#define let const auto
namespace Jde::Access{
	constexpr sv format{ "subscription {0}{2}{{ {1}{2}(subscriptionId:$id{4}){{{3}}} }}" }; //subscription UserCreated{ userCreated(subscriptionId:129){id} }
	α EventTypeSubscribeAwait::Subscribe()ι->TAwait<vector<QL::SubscriptionId>>::Task{
		using enum ESubscription;
		let capitalized = DB::Names::Capitalize( _name );
		auto vars = [&]( ESubscription event )->jobject {
			jobject vars = _vars;
			vars["id"] = underlying( _type | event );
			return vars;
		};
		try{
			if( !empty(_events & Created) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Created", _cols, _args), vars(Created), _listener, _executer );
			if( !empty(_events & Deleted) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Deleted", _cols, _args), vars(Deleted), _listener, _executer );
			if( !empty(_events & Restored) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Restored", _cols, _args), vars(Restored), _listener, _executer );
			if( !empty(_events & Purged) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Purged", _cols, _args), vars(Purged), _listener, _executer );
			if( !empty(_events & Added) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Added", _cols, _args), vars(Added), _listener, _executer );
			if( !empty(_events & Removed) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Removed", _cols, _args), vars(Removed), _listener, _executer );
			if( !empty(_events & Updated) )
				co_await *_qlServer->Subscribe( Ƒ(format, capitalized, _name, "Updated", _cols, _args), vars(Updated), _listener, _executer );
			Resume();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α EventsSubscribeAwait::Execute()ι->EventTypeSubscribeAwait::Task{
		using enum ESubscription;
		//No schemas = every schema, and then no predicate and no variable at all:  an empty array binds as an In filter matching
		//nothing, and an unbound $schemas extrapolates to null - which the resources predicate would reject and RoleMAwait's
		//isApplicable would compare a permission's schema against and drop every roleAdded.
		let all = _schemas.empty();
		jobject vars;
		if( !all )
			vars["schemas"] = boost::json::value_from( _schemas );
		const string byArg = all ? "" : ", schemaName:$schemas";
		const string byName = all ? "" : "(schemaName:$schemas)";
		const string bySchema = all ? "" : "(schema:$schemas)";
		try{
			co_await EventTypeSubscribeAwait{ _qlServer, "user", User, "id", {}, Created | Deleted | Restored | Purged, {}, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "group", Group, "id", {}, Deleted | Restored | Purged, {}, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "group", Group, "id memberId", {}, Added | Removed, {}, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "role", Role, "id", {}, Deleted | Restored | Purged, {}, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "role", Role, Ƒ("id permissionRight{{id allowed denied resource{}{{id slug schemaName slug criteria deleted}}}} role{{id}}", byName), {}, Added | Removed, vars, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "resources", Resources, "id schemaName slug criteria deleted", byArg, Created, vars, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "resources", Resources, "id schemaName slug", byArg, Deleted | Restored, vars, _executer, _listener }; //schemaName, as Created above and every producer spell it (access-review3 #23) - `schema` was no column, so an id-less delete could never be resolved by name.
			co_await EventTypeSubscribeAwait{ _qlServer, "permissionRight", Permission, Ƒ("id allowed denied resource{}", bySchema), {}, Updated, vars, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "acl", Acl, Ƒ("identity{{id}} permissionRight{{id allowed denied resource{}{{id}}}} role{{id}}", bySchema), {}, Created, vars, _executer, _listener };
			co_await EventTypeSubscribeAwait{ _qlServer, "acl", Acl, Ƒ("identity{{id}} permissionRight{{id resource{}}} role{{id}}", bySchema), {}, Purged, vars, _executer, _listener };
			Resume();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}