#include "RoleLoadAwait.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/ql/IQL.h>

#define let const auto
namespace Jde::Access{
	constexpr ELogTags _tags{ ELogTags::Access };

	α RoleLoadAwait::Load()ι->VoidTask{
		try{
			auto permissionsQuery = _qlServer->QueryArray( "roles{ id deleted permissionRights{id} }", {}, _executer );
			auto rolesQuery = _qlServer->QueryArray( "roles{ id deleted roles{id} }", {}, _executer );
			let [permissions, roles] = co_await All( *permissionsQuery, *rolesQuery );//at once (#345):  the members merge in once both are in.
			flat_map<RolePK,Role> y;
			for( let& value : permissions ){
				const Role role{ Json::AsObject(value) };
				y.emplace( role.PK, role );
			}

			for( let& value : roles ){
				const Role role{ Json::AsObject(value) };
				if( auto p = y.find(role.PK); p!=y.end() )
					p->second.Members.insert( role.Members.begin(), role.Members.end() );
				else
					y.emplace( role.PK, role );
			}
			for( let& [pk, role] : y )
				TRACET( _tags | ELogTags::Pedantic, "[{}]AddedRole membersSize={}", role.PK.Value, role.Members.size() );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}