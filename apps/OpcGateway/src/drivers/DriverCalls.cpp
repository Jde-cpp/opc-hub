#include "DriverCalls.h"
#include <jde/access/Authorize.h>
#include <jde/opc/UAException.h>
#include <jde/opc/server/UAServer.h>
#include "../GatewayAppClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	//The status, and why where there is a why, as a line's end:  a detail is a sentence of its own.
	Ω answered( UA_StatusCode result, sv detail )ι->string{
		let status = result ? UAException::Message( result ) : "Good";
		return detail.empty() ? Ƒ( "{}.", status ) : Ƒ( "{} - {}{}", status, detail, detail.ends_with('.') ? "" : "." );
	}

	α DriverCalls::What( UserPK user, sv call )Ι->string{
		let acl = AppClient()->Acl();//null for a client that never authorizes.
		return Ƒ( "Call by '{}' of {} on '{}'", acl ? acl->UserName(user) : std::to_string(user.Value), call, _slug );
	}
	α DriverCalls::Hold( const UA_Variant* output, UserPK user, sv call )ι->Call{
		Call y{ output, 0, What(user, call) };
		ul _{ _mutex };
		y.Id = ++_last;
		_held.insert_or_assign( output, y.Id );
		return y;
	}
	α DriverCalls::Held( const Call& call )Ι->bool{
		rl _{ _mutex };
		let p = _held.find( call.Output );
		return p!=_held.end() && p->second==call.Id;
	}
	α DriverCalls::Take( const Call& call )ι->bool{
		ul _{ _mutex };
		let p = _held.find( call.Output );
		if( p==_held.end() || p->second!=call.Id )
			return false;
		_held.erase( p );
		return true;
	}
	α DriverCalls::Cancelled( const void* output )ι->void{
		ul _{ _mutex };
		_held.erase( static_cast<const UA_Variant*>(output) );
	}
	α DriverCalls::Size()Ι->uint{
		rl _{ _mutex };
		return _held.size();
	}
	α DriverCalls::Answer( UA_Server& server, Call call, UA_StatusCode result, string detail )ι->void{
		Opc::Server::UAServer::Post( server, [self=shared_from_this(), call=move(call), result, detail=move(detail)]( UA_Server& ua ){
			let answer = answered( result, detail );
			if( !self->Take(call) ){
				INFO( "{} answered after open62541 cancelled it - a timeout, a client's Cancel, the queue's limit or the shutdown:  {}", call.What, answer );//its cancel names no reason.
			}
			else if( let sc = UA_Server_setAsyncCallMethodResult(&ua, const_cast<UA_Variant*>(call.Output), result); sc ){
				WARN( "{} couldn't be answered ({}):  {}", call.What, UAException::Message(sc), answer );
			}
			else{
				INFO( "{}:  {}", call.What, answer );
			}
		} );
	}
	α DriverCalls::Refused( UserPK user, sv call, UA_StatusCode result, sv detail )Ι->void{ INFO( "{}:  {}", What(user, call), answered(result, detail) ); }
	α DriverCalls::Dropped( const Call& call, sv why )Ι->void{ INFO( "{} never sent:  {}", call.What, why ); }
}