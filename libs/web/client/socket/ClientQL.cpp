#include <jde/web/client/socket/ClientQL.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Subscription.h>

#define let const auto

namespace Jde::Web::Client{
	//Every ClientQL answer is the socket's reply reshaped for the caller - this awaits the reply and applies `convert`, which is the
	//one thing the four entry points differ in.  TInner: the session's ClientSocketAwait<>.
	template<class T, class TInner>
	struct ConvertAwait final : TAwaitEx<T,typename TInner::Task>{
		using base = TAwaitEx<T,typename TInner::Task>;
		using TReply = decltype( std::declval<TInner&>().await_resume() );
		ConvertAwait( TInner&& inner, function<T(TReply&&)> convert, SL sl )ι: base{sl}, _inner{move(inner)}, _convert{move(convert)}{}
		α Execute()ι->TInner::Task override{
			try{
				base::Resume( _convert(co_await _inner) );
			}
			catch( runtime_error& e ){
				base::ResumeExp( move(e) );
			}
		}
	private:
		TInner _inner;
		function<T(TReply&&)> _convert;
	};

	//The session's reply to `query`, converted; a session that is gone fails the await with the same exception it always has.
	template<class T>
	Ω query( const wp<IClientSocketSession>& weak, string&& query, jobject&& variables, bool returnRaw, function<T(jvalue&&)> convert, SL sl )ι->up<TAwait<T>>{
		auto session = weak.lock();
		if( !session )
			return mu<ExceptionAwait<T>>( mu<Exception>("Client socket session closed.", ExceptionArgs{}, sl), sl );
		return mu<ConvertAwait<T,ClientSocketAwait<jvalue>>>( session->Query(move(query), move(variables), returnRaw, sl), move(convert), sl );
	}

	α ClientQL::Subscribe( string&& query, jobject variables, sp<QL::IListener> listener, UserPK /*executer*/, SL sl )ε->up<TAwait<vector<QL::SubscriptionId>>>{
		auto session = _session.lock();
		THROW_IF( !session, "Client socket session closed." );
		return mu<ConvertAwait<vector<QL::SubscriptionId>,ClientSocketAwait<jarray>>>( session->Subscribe(move(query), move(variables), listener, sl), []( jarray&& ids ){ return Json::FromArray<QL::SubscriptionId>( ids ); }, sl );
	}

	α ClientQL::Unsubscribe( sp<QL::IListener> /*listener*/, vector<QL::SubscriptionId> ids, SL sl )ι->void{
		if( auto session = _session.lock(); session )
			session->Unsubscribe( move(ids), sl );
	}

	α ClientQL::Query( string q, jobject variables, UserPK, bool returnRaw, SL sl )ι->up<TAwait<jvalue>>{
		return query<jvalue>( _session, move(q), move(variables), returnRaw, []( jvalue&& v ){ return move(v); }, sl );
	}
	α ClientQL::QueryObject( string q, jobject variables, UserPK /*executer*/, bool returnRaw, SL sl )ε->up<TAwait<jobject>>{
		return query<jobject>( _session, move(q), move(variables), returnRaw, [sl]( jvalue&& v )->jobject{
			if( v.is_object() )
				return move( v.get_object() );
			if( v.is_null() )
				return {};
			throw Exception{ "Expected object.", {}, sl };
		}, sl );
	}
	α ClientQL::QueryArray( string q, jobject variables, UserPK /*executer*/, bool returnRaw, SL sl )ε->up<TAwait<jarray>>{
		return query<jarray>( _session, move(q), move(variables), returnRaw, [sl]( jvalue&& v )->jarray{
			if( v.is_array() )
				return move( v.get_array() );
			throw Exception{ "Expected array.", {}, sl };
		}, sl );
	}
}