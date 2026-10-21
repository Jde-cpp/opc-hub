#pragma once
#include <thread>
#include <jde/access/Authorize.h>
#include <jde/app/client/IAppClient.h>

//What a suite needs to enforce resources and grant on them through the AppServer, for any app's suite:  the gateway's and the
//OpcServer's use them.
namespace Jde{
	//`pred` polled every `interval` until it holds;  false once `timeout` passes, though it is always asked once.
	Ξ WaitUntil( function<bool()> pred, Duration timeout=std::chrono::seconds{10}, Duration interval=std::chrono::milliseconds{10} )ι->bool{
		for( const auto deadline = std::chrono::steady_clock::now()+timeout; !pred(); std::this_thread::sleep_for(interval) ){
			if( std::chrono::steady_clock::now()>deadline )
				return false;
		}
		return true;
	}
	//`schema`'s `slug` resource - the row for `criteria`, or the root - enforced, or soft-deleted again as it ships:  `on` is
	//the switch, so restore enforces.  Returns once `authorizer` has heard, which it does over its subscription after the
	//mutation returns.
	Ξ EnforceResource( App::Client::IAppClient& app, Access::Authorize& authorizer, sv schema, sv slug, bool on, sv criteria={} )ε->void{
		app.QuerySync<jvalue>( Ƒ("{}Resource( schemaName:$schema, slug:$slug, criteria:$criteria )", on ? "restore" : "delete"), {{"schema", schema}, {"slug", slug}, {"criteria", criteria.size() ? jvalue{criteria} : jvalue{}}} );
		THROW_IF( !WaitUntil([&]{ return authorizer.FindActiveResourcePK(string{schema}, string{slug}, string{criteria}).has_value()==on; }), "{}/{}{}'s {} didn't reach the authorizer.", schema, slug, criteria.size() ? Ƒ("[{}]", criteria) : string{}, on ? "restore" : "delete" );
	}
	//One of a role's grants:  `Allowed`/`Denied` on `Schema`'s `Slug` resource - the row for `Criteria`, or the root - with
	//`Name` for the row a criteria grant creates.
	struct RoleGrant final{ sv Schema; sv Slug; Access::ERights Allowed; Access::ERights Denied{}; sv Criteria{}; sv Name{}; };
	//`user` holds `grants` through a role of its own, `role`, made with them the first time and kept after:  the suites share one
	//db in a run, and on a persisted one a second create trips the unique indexes.  A role rather than a direct grant:  a direct
	//grant's acl event reaches every app's authorizer, an embedded OpcServer's among them, which never saw the granting app's
	//row and asserts (the acl subscriptions' schema filter, reviews/soak-findings.md).
	Ξ GrantRole( App::Client::IAppClient& app, sv role, UserPK user, const vector<RoleGrant>& grants )ε->Access::RolePK{
		if( const auto found = app.QuerySync("role(slug:$slug, deleted:null){id}", {{"slug", role}}); !found.empty() )
			return Access::RolePK{ Json::AsNumber<Access::RolePK::Type>(found, "id") };
		const auto roleId = Json::AsNumber<Access::RolePK::Type>( app.QuerySync<jobject>("createRole( slug:$slug, name:$slug ){id}", {{"slug", role}}), "id" );
		for( const auto& grant : grants ){
			jobject vars{ {"roleId", roleId}, {"allowed", underlying(grant.Allowed)}, {"denied", underlying(grant.Denied)}, {"schema", grant.Schema}, {"resource", grant.Slug} };
			string resource{ "schemaName:$schema, slug:$resource" };
			if( grant.Criteria.size() ){
				vars["criteria"] = grant.Criteria;
				resource += ", criteria:$criteria";
			}
			if( grant.Name.size() ){
				vars["name"] = grant.Name;
				resource += ", name:$name";
			}
			app.QuerySync<jvalue>( Ƒ("addRole( id:$roleId, permissionRight:{{allowed:$allowed, denied:$denied, resource:{{{}}}}} )", resource), move(vars) );
		}
		app.QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", user.Value}, {"roleId", roleId}} );
		return Access::RolePK{ roleId };
	}
	//The role of one grant, `allowed` on `schema`'s `slug` resource - the row for `criteria`, or the root.
	Ξ GrantRole( App::Client::IAppClient& app, sv role, sv schema, sv slug, sv criteria, Access::ERights allowed, UserPK user )ε->Access::RolePK{
		return GrantRole( app, role, user, {{schema, slug, allowed, {}, criteria}} );
	}
}