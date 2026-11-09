#include <jde/app/client/clientSubscriptions.h>
#include <jde/ql/types/Subscription.h>
#include <jde/app/client/IAppClient.h>

#define let const auto
namespace Jde::App::Client{
	static constexpr ELogTags _subTags{ ELogTags::QL | ELogTags::Client };
	static vector<Subscriptions::Request> _requests; static std::mutex _requestMutex;
	constexpr uint _maxReplayFailures{ 3 };
	Ω findRequest( vector<Subscriptions::Request>& requests, const sp<QL::IListener>& listener, str query, const jobject& variables )ι->vector<Subscriptions::Request>::iterator{
		return find_if( requests, [&](let& r){ return r.Listener==listener && r.Query==query && r.Variables==variables; } );
	}
	α Subscriptions::Remember( const IAppClient* client, string query, jobject variables, sp<QL::IListener> listener, const flat_set<QL::SubscriptionId>& ids )ι->void{
		lg _{ _requestMutex };
		auto p = findRequest( _requests, listener, query, variables );
		if( p==_requests.end() )
			_requests.push_back( Request{client, move(query), move(variables), move(listener), ids} );
		else{
			p->Ids = ids;
			p->Failures = 0;//L5: this only runs on an ack, so reaching here is the server accepting the request - the count starts over.
		}
	}
	α Subscriptions::Forget( const sp<QL::IListener>& listener, const flat_set<QL::SubscriptionId>& ids )ι->void{
		lg _{ _requestMutex };
		std::erase_if( _requests, [&](auto& r){
			if( r.Listener!=listener )
				return false;
			if( ids.empty() )
				return true;
			std::erase_if( r.Ids, [&](let id){ return ids.contains(id); } );
			return r.Ids.empty();//a request only goes when nothing it produced is still subscribed.
		});
	}
	α Subscriptions::NoteReplayFailure( const Request& request )ι->bool{
		lg _{ _requestMutex };
		auto p = findRequest( _requests, request.Listener, request.Query, request.Variables );
		if( p==_requests.end() )
			return false;//forgotten while the replay was in flight - an Unsubscribe, or a listener that went away.
		if( ++p->Failures<_maxReplayFailures )
			return false;
		_requests.erase( p );
		return true;
	}
	α Subscriptions::Remembered()ι->vector<Request>{
		lg _{ _requestMutex };
		return _requests;
	}
	//Subscribe only builds the await - the write happens when it is suspended on - so each one has to be awaited, and that
	//has to happen off the connect path.  Sequential rather than fanned out: they share one socket, and an ack has to land
	//before the next request reuses the id space.
	Ω replay( sp<IAppClient> client, vector<Subscriptions::Request> requests )ι->Web::Client::ClientSocketAwait<jarray>::Task{
		uint done{};
		for( let& r : requests ){
			try{
				co_await client->Subscribe( string{r.Query}, r.Variables, r.Listener );
				++done;
			}
			catch( runtime_error& e ){
				//One LOG rather than an if/else: the level macros expand to an `if`, so a bare `else` after one binds to theirs.
				let dropped = Subscriptions::NoteReplayFailure( r );
				LOG( dropped ? ELogLevel::Error : ELogLevel::Warning, _subTags, "Could not replay subscription '{}'{}: {}", r.Query, dropped ? Ƒ(" - dropped, refused on {} consecutive replays", _maxReplayFailures) : string{}, e.what() );
			}
		}
		INFOT( _subTags, "Replayed {} of {} subscription request(s) on the new session.", done, requests.size() );
	}
	α Subscriptions::Replay( sp<IAppClient> client )ι->uint{
		auto requests = Remembered();
		std::erase_if( requests, [&]( let& r ){ return r.Client!=client.get(); } );//another client's are its own session's to carry.
		let count = (uint)requests.size();//answered before any await: Client::Connect needs to know now whether this is a reconnect.
		if( count )
			replay( move(client), move(requests) );
		return count;
	}
}