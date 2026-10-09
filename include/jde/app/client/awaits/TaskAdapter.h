#pragma once
#include <jde/web/client/socket/ClientSocketAwait.h>

namespace Jde::App::Client{
	//A ClientSocketAwait<T> as a TAwait<T>:  IApp's SessionInfoAwait/AddSession return up<TAwait<T>> so the in-process
	//implementations fit beside the socket's, and the socket awaitable's ::Task is TTimedTask<T>.  B1 removes the need.
	template<class T>
	struct TaskAdapter final : TAwait<T>{
		TaskAdapter( Web::Client::ClientSocketAwait<T>&& inner, SRCE )ι:TAwait<T>{sl}, _inner{ move(inner) }{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->typename Web::Client::ClientSocketAwait<T>::Task{
			try{
				this->Resume( co_await _inner );
			}
			catch( runtime_error& e ){
				this->ResumeExp( move(e) );
			}
		}
		Web::Client::ClientSocketAwait<T> _inner;
	};
}
