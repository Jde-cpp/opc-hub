#include "Guest.h"
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/AccessListener.h>
#include <jde/access/Authorize.h>
#include <jde/web/server/Sessions.h>
#include "LocalClient.h"

#define let const auto
namespace Jde::App::Server{
	constexpr ELogTags _tags{ ELogTags::Sessions };
	static string _slug;
	static std::atomic<UserPK::Type> _userPK{};//the slug's user, live or deleted, 0 while none is known:  what an event by id alone - a restore, a delete, a purge - is matched against.

	Ω setGuest( UserPK userPK )ι->void{
		_userPK = userPK.Value;
		Web::Server::Sessions::SetGuest( userPK );
		INFO( "[{}]Sessions with no sign-in are user '{}'s.", userPK.Value, _slug );
	}
	Ω clearGuest( UserPK userPK, sv what, sv until )ι->void{
		Web::Server::Sessions::SetGuest( {} );
		WARN( "[{}]The guest user '{}' was {} - sessions with no sign-in are anonymous until {}.", userPK.Value, _slug, what, until );
	}

	α Guest::Resolve()ε->void{
		if( _slug.empty() )
			return;
		let user = BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{"user( slug:$slug ){ id deleted }", jobject{{"slug", _slug}}, UserPK{UserPK::System}, QLPtr()} );//deleted or not:  a deleted one is known, so its restore by id is matched.
		let id = Json::FindNumber<UserPK::Type>( user, "id" );
		_userPK = id.value_or( 0 );
		if( id && !Json::FindTimePoint(user.get_object(), "deleted") )
			setGuest( UserPK{*id} );
		else{
			Web::Server::Sessions::SetGuest( {} );
			WARN( "/access/guest '{}' names {} - sessions with no sign-in are anonymous until {}.", _slug, id ? "a deleted user" : "no user", id ? "it is restored" : "one is created" );
		}
	}

	//Before Access::Server::Configure subscribes the listener, so no event reads OnUser while it is assigned:  until Resolve, _userPK
	//is 0, and a user created of the slug meanwhile is the guest, as Resolve will find it.
	α Guest::Configure( Access::AccessListener& listener )ι->void{
		_slug = Settings::FindString( "/access/guest" ).value_or( "" );
		if( _slug.empty() )
			return;
		listener.OnUser = []( UserPK userPK, Access::ESubscription event, const jobject& user ){
			using enum Access::ESubscription;
			let slug = Json::FindSV( user, "slug" );//where the mutation named one:  a create's always, a delete's or restore's by slug, a rename's.
			let named = slug==_slug, known = userPK.Value==_userPK;
			if( event==Created || event==Restored ){
				if( named || (!slug && known) )
					setGuest( userPK );
			}
			else if( event==Deleted || event==Purged ){
				if( !known )
					return;
				if( Web::Server::Sessions::Guest() )
					clearGuest( userPK, event==Deleted ? "deleted" : "purged", event==Deleted ? "it is restored" : "one of that slug is created" );
				if( event==Purged )
					_userPK = 0;
			}
			else if( event==Updated && slug ){//a slug renamed:  another user's to the guest's, or the guest's away.
				if( named && !known ){
					if( Authorizer()->IsLiveUser(userPK) )
						setGuest( userPK );
					else{//as Resolve has it:  known, so its restore by id is matched, but no guest.
						_userPK = userPK.Value;
						Web::Server::Sessions::SetGuest( {} );
						WARN( "[{}]A deleted user was renamed '{}', the guest's slug - sessions with no sign-in are anonymous until it is restored.", userPK.Value, _slug );
					}
				}
				else if( known && !named ){
					clearGuest( userPK, Ƒ("renamed '{}'", *slug), "one of that slug is created" );
					_userPK = 0;
				}
			}
		};
	}
}