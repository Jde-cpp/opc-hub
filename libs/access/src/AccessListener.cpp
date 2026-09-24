#include <jde/access/AccessListener.h>
#include <jde/ql/ql.h>
//#include <jde/ql/IQL.h>
//#include <jde/ql/SubscriptionAwait.h>
#include <jde/access/Authorize.h>
#include "accessInternal.h"

#define let const auto

namespace Jde::Access{
	α AccessListener::Shutdown( bool terminate, SL )ι->void{
		if( terminate )
			return;
		_qlServer->Unsubscribe( sp<QL::IListener>{sp<void>{}, this}, {} );
	}
	α AccessListener::OnChange( const jvalue& j, QL::SubscriptionId clientId )ε->void{
		let& root = Json::AsObject(j);
		let nameValue = root.begin();
		if( nameValue==root.end() )
			return;
		let& object = Json::AsObject( nameValue->value() );
		let event = (ESubscription)clientId;
		using enum ESubscription;
		if( !empty(event & Acl) ){
			AclChanged( event & ~Acl, object );
			return;
		}

		let id = Json::FindNumber<uint32>( object, "id" );
		if( !empty(event & Resources) && (id || object.contains("slug")) ){ //no id means the fan-out could not pick one row - a by-slug delete that hit several - and the slug (with the schema, when the mutation named one) names every row it hit, which UpdateResourceDeleted applies to all of them (access-review3 #22).
			ResourceChanged( id.value_or(0), event & ~Resources, object );
			return;
		}
		if( !id ){
			WARNT( ELogTags::Access, "[{}]a notification for event {:x} carried no id, and nothing else finds the row - the access cache is stale for it until a reload: {}", Name, (uint16)underlying(event), serialize(object) );
			return;
		}
		let pk = *id;
		if( !empty(event & User) )
			UserChanged( {pk}, event & ~User, object );
		else if( !empty(event & Group) )
			GroupChanged( {pk}, event & ~Group, object );
		else if( !empty(event & Role) )
			RoleChanged( pk, event & ~Role, object );
		else if( !empty(event & Permission) )
			PermissionUpdated( pk, object );
	}
#pragma GCC diagnostic ignored "-Wswitch"
	α AccessListener::UserChanged( UserPK userPK, ESubscription event, const jobject& )ι->void{
		using enum ESubscription;
		switch( event ){
			case Created: Authorizer().CreateUser( userPK ); break;
			case Deleted: Authorizer().DeleteUser( userPK ); break;
			case Restored: Authorizer().RestoreUser( userPK ); break;
			case Purged: Authorizer().PurgeUser( userPK ); break;
		}
	}
	α AccessListener::GroupChanged( GroupPK groupPK, ESubscription event, const jobject& o )ε->void{
		using enum ESubscription;
		switch( event ){
			case Added:
			case Removed:{
				flat_set<IdentityPK::Type> members;
				Json::Visit( Json::AsValue(o, "memberId"), [&](const jvalue& v){ members.insert( Json::AsNumber<IdentityPK::Type>(v) );} );
				if( event==Added )
					Authorizer().AddToGroup( groupPK, members );
				else
					Authorizer().RemoveFromGroup( groupPK, members );
			}break;
			case Deleted:
				Authorizer().DeleteGroup( groupPK );
				break;
			case Restored:
				Authorizer().RestoreGroup( groupPK );
				break;
			case Purged:
				Authorizer().PurgeGroup( groupPK );
				break;
		}
	}
	α AccessListener::RoleChanged( RolePK rolePK, ESubscription event, const jobject& o )ε->void{
		using enum ESubscription;
		switch( event ){
			case Deleted: Authorizer().DeleteRestoreRole( rolePK, true ); break;
			case Restored: Authorizer().DeleteRestoreRole( rolePK, false ); break;
			case Purged: Authorizer().PurgeRole( rolePK ); break;
			case Added:
			case Removed:{
				if( auto rights = Json::FindObject(o, "permissionRight"); rights ){
					if( event==Added ){
						Access::Permission permission{ *rights };
						Authorizer().AddRolePermission( rolePK, permission.PK, (ERights)permission.Allowed, (ERights)permission.Denied, rights->at("resource").as_object() );
					}
					else{
						flat_set<PermissionRightsPK> members;
						Json::Visit( Json::AsValue(o, "permissionRight/id"), [&](const jvalue& v){ members.insert( Json::AsNumber<PermissionRightsPK>(v) );} );
						Authorizer().RemoveRoleChildren( rolePK, members );
					}
				}
				else if( auto child = Json::FindObject(o, "role"); child ){
					if( event==Added )
						Authorizer().AddRoleChild( rolePK, Json::ToVector<RolePK>(Json::AsValue(*child, "id")) );
					else{
						flat_set<PermissionRightsPK> members;
						Json::Visit( Json::AsValue(*child, "id"), [&](const jvalue& v){ members.insert( Json::AsNumber<RolePK>(v) );} );//child's id - o["id"] is the parent role.
						Authorizer().RemoveRoleChildren( rolePK, members );
					}
				}
			}break;
		}
	}
	α AccessListener::ResourceChanged( ResourcePK resourcePK, ESubscription event, const jobject& o )ε->void{
		using enum ESubscription;
		switch( event ){
			case Created:
				if( !resourcePK ){ //an insert's id comes back from its proc's out row, so this is not expected - but pk 0 in the cache would be worse than a stale one.
					WARNT( ELogTags::Access, "[{}]a resource Created notification carried no id - not cached: {}", Name, serialize(o) );
					break;
				}
				Authorizer().CreateResource( {resourcePK, o} );
				break;
			case Deleted:
			case Restored:
				Authorizer().UpdateResourceDeleted( resourcePK, Json::FindDefaultSV(o, "schemaName"), o, event==Restored ); //schemaName - the column is called that (access-review3 #23); "schema" read empty and the by-name fallback could never match.
				break;
		}
	}
	α AccessListener::PermissionUpdated( PermissionRightsPK pk, const jobject& o )ε->void{
		let allowed = Json::FindNumber<uint8>( o, "allowed" );
		let denied = Json::FindNumber<uint8>( o, "denied" );
		Authorizer().UpdatePermission( pk, allowed ? optional<ERights>((ERights)*allowed) : nullopt, denied ? optional<ERights>((ERights)*denied) : nullopt );
	}
	α AccessListener::AclChanged( ESubscription event, const jobject& o )ε->void{
		using enum ESubscription;
		let identityPK = Json::AsNumber<IdentityPK::Type>( o, "identity/id" );
		//The only account of the live acl path: without it an acl that never reached this process looks exactly like one
		//that reached it and changed nothing (soak-findings #4).
		DBGT( ELogTags::Access, "[{}]acl event {:x} for identity {}: {}", Name, (uint16)underlying(event), identityPK, serialize(o) );
		switch( event ){
			case Created:{
				if( auto v = o.if_contains("permissionRight"); v ){ //identity{id:y}, permission:{ allowed:x, denied:x, resource:{id:x} }
					let& permission = Json::AsObject(*v);
					Authorizer().AddAcl(
						identityPK,
						Json::AsNumber<PermissionRightsPK>( permission, "id" ),
						(ERights)Json::FindNumber<uint8>( permission, "allowed" ).value_or(0),
						(ERights)Json::FindNumber<uint8>( permission, "denied" ).value_or(0),
						Json::AsNumber<ResourcePK>( permission, "resource/id" )
					);
				}
				else if( auto role = o.if_contains("role"); role ) //identity{id:y}, role:{ id:x }
					Authorizer().AddAcl( Json::AsNumber<IdentityPK::Type>( o, "identity/id" ), QL::AsId<RolePK>(*role) );
			}break;
			case Purged:{
				optional<PermissionRole> permissionPK;
				if( auto p = o.if_contains("permissionRight"); p ) //identity{id:y}, permissionRight:{ allowed:x, denied:x, resource:{id:x} }
					permissionPK = PermissionRole{ std::in_place_index<0>, QL::AsId<PermissionRightsPK>(*p) };
				else if( auto role = o.if_contains("role"); role ) //identity{id:y}, role:{ id:x }
					permissionPK = PermissionRole{ std::in_place_index<1>, QL::AsId<RolePK>(*role) };
				if( permissionPK )
					Authorizer().RemoveAcl( identityPK, *permissionPK );
			}break;
		}
	}
}