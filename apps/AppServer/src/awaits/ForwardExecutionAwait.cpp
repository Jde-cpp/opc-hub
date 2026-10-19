#include "ForwardExecutionAwait.h"
#include <jde/app/proto/app.FromServer.h>
#include "../ServerSocketSession.h"
#include "../WebServer.h"
#define let const auto

namespace Jde::App::Server{
	struct SessionHandle final{
		sp<ServerSocketSession> RequestSocketSession;
		ForwardExecutionAwait::Handle Handle;
		ConnectionPK TargetConnectionPK;//the instance the request was sent to - OnCloseConnection cancels the forwards addressed to a connection that has gone.  Which session may *answer* is the key's job, below.
	};
	//Keyed by target connection + request id, not the id alone.  The id comes from the target session's own counter
	//(IWebsocketSession::NextRequestId), the same one the QueryClient calls we send it draw from, so a reply id can only
	//belong to one of the two maps - id spaces that used to overlap delivered a forward's kException to a pending
	//QueryClient and left the forward parked (finding #12).  That counter is per-session, so two gateways both hand out
	//id 1 and the connection has to be part of the key; it also makes "only the instance the request was sent to may
	//answer" (finding #5) a property of the key rather than a check.
	Ξ forwardKey( ConnectionPK targetConnectionPK, RequestId requestId )ι->uint{ return uint(targetConnectionPK&0xFFFFFFFF)<<32 | (requestId&0xFFFFFFFF); }//uint32 is uint_fast32_t - 64 bits here - so mask before packing; the wire fields are both 32-bit.
	concurrent_flat_map<uint,SessionHandle> _forwardExecutionMessages;
	ForwardExecutionAwait::ForwardExecutionAwait( UserPK userPK, Proto::FromClient::ForwardExecution&& customRequest, sp<ServerSocketSession> serverSocketSession, SL sl )ι:
		base{ sl },
		_userPK{ userPK },
		_forwardExecutionMessage{ move(customRequest) },
		_requestSocketSession{ serverSocketSession }
	{}

	α ForwardExecutionAwait::Suspend()ι->void{
		let rawInstancePK = _forwardExecutionMessage.app_instance_pk();
		optional<ProgInstPK> instancePK = rawInstancePK ? optional<ProgInstPK>{rawInstancePK} : nullopt;//0 = any instance of the app (App.FromClient.proto); a raw uint32 would never be nullopt, defeating the wildcard.
		sp<ServerSocketSession> target;
		try{
			target = Server::FindApp( _forwardExecutionMessage.app_pk(), instancePK );//resolve the target before anything goes on the wire; this is the only step that can fail, since IWebsocketSession::Write is noexcept.
		}
		catch( runtime_error& e ){ //Could not find app instance.
			ResumeExp( move(e) );//SetExp + h.resume() runs this coroutine's catch to completion and (suspend_never final_suspend) destroys its frame - nothing was added to the map, so no dead handle survives (finding #7).
			return;
		}
		let targetConnectionPK = target->ConnectionPK();
		let serverRequestId = target->NextRequestId();//the target's own counter - see forwardKey.
		//Build the request before the emplace: once the entry is in the map another thread may resolve it and destroy this
		//frame, so no member of *this may be touched below.
		auto msg = FromServer::ExecuteRequest( serverRequestId, _userPK, move(*_forwardExecutionMessage.mutable_execution_transmission()) );
		//Emplace before the Write, keyed to the connection FindApp resolved (not the wildcard-able app_instance_pk), so only
		//that session can answer (finding #5).  The io_context runs on several threads, so the target's strand can process
		//the request and reply before this returns; an entry added afterwards would be a handle nobody ever resolves and a
		//forward parked until disconnect (finding #11).
		_forwardExecutionMessages.emplace( forwardKey(targetConnectionPK, serverRequestId), SessionHandle{move(_requestSocketSession), _h, targetConnectionPK} );
		target->Write( move(msg) );
	}

	α ForwardExecutionAwait::OnCloseConnection( Web::Server::SocketId requesterId, ConnectionPK targetConnectionPK )ι->void{
		_forwardExecutionMessages.erase_if( [=](auto&& kv){
			let requesterDied = kv.second.RequestSocketSession->Id()==requesterId;//match the requester by its unique socket id, not ConnectionPK - that is 0 for every unregistered web client, so one disconnect would cancel them all.
			let targetDied = targetConnectionPK && kv.second.TargetConnectionPK==targetConnectionPK;//the instance we forwarded to is gone; its reply will never arrive, so fail the forward instead of leaking it (there is no timeout on this path).
			if( !requesterDied && !targetDied )
				return false;
			auto h = kv.second.Handle;
			h.promise().ResumeExp( Exception{targetDied ? "Forward target disconnected." : "Connection closed."}, h );
			return true;
		} );
	}
	α ForwardExecutionAwait::ResumeExp( Exception&& e, RequestId serverRequestId, ConnectionPK responder )ι->bool{
		if( !responder )//an unregistered session is never a forward target, and 0 is the ConnectionPK every one of them shares.
			return false;
		return _forwardExecutionMessages.erase_if( forwardKey(responder, serverRequestId), [&](auto&& kv){
			auto h = kv.second.Handle;
			h.promise().ResumeExp( move(e), h );
			return true;
		});
	}
	α ForwardExecutionAwait::Resume( string&& results, RequestId serverRequestId, ConnectionPK responder )ι->bool{
		if( !responder )
			return false;
		return _forwardExecutionMessages.erase_if( forwardKey(responder, serverRequestId), [&](auto&& kv){
			auto h = kv.second.Handle;
			h.promise().Resume( move(results), h );
			return true;
		});
	}
}