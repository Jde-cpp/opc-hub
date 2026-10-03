#include "RoleLoadAwait.h"
#include <jde/ql/IQL.h>

#define let const auto
namespace Jde::Access{
	constexpr ELogTags _tags{ ELogTags::Access };

	α RoleLoadAwait::Load()ι->QL::QLAwait<jarray>::Task{
		try{
			flat_map<RolePK,Role> y;
			let permissionQL = "roles{ id deleted permissionRights{id} }";
			let permissions = co_await *_qlServer->QueryArray( permissionQL, {}, _executer );
			for( let& value : permissions ){
				const Role role{ Json::AsObject(value) };
				y.emplace( role.PK, role );
			}

			let roleQL = "roles{ id deleted roles{id} }";
			let roles = co_await *_qlServer->QueryArray( roleQL, {}, _executer );
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