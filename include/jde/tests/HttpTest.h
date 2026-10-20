#pragma once
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>

namespace Jde{
	//A fixture's blocking calls to this process's own listener, by port.  Plain http - the server detects ssl per connection.
	struct HttpTest : ::testing::Test{
		constexpr static sv Host{ "127.0.0.1" };//sessions are endpoint-bound; connect by ip so the socket's remote address matches the endpoint sessions are minted with.
		Ω Get( PortType port, string target, string authorization={} )->Web::Client::ClientHttpRes{
			return BlockAwait<Web::Client::ClientHttpAwait,Web::Client::ClientHttpRes>( Web::Client::ClientHttpAwait{string{Host}, move(target), port, {.Authorization=move(authorization), .IsSsl=false}} );
		}
		Ω Post( PortType port, string target, string body, string authorization={}, string contentType="application/json", string origin={} )->Web::Client::ClientHttpRes{
			return BlockAwait<Web::Client::ClientHttpAwait,Web::Client::ClientHttpRes>( Web::Client::ClientHttpAwait{string{Host}, move(target), move(body), port, {.Authorization=move(authorization), .Origin=move(origin), .ContentType=move(contentType), .IsSsl=false}} );
		}
		Ω QL( PortType port, string query, string authorization={} )->jvalue{//`data`: an object for a query, an array for a mutation.
			return Post( port, "/graphql", serialize(jobject{{"query", move(query)}}), move(authorization) ).Json().at( "data" );
		}
		//the http status `call` failed with;  nullopt when it succeeded, or failed without one.
		Ω FailureStatus( function<void()> call )->optional<http::status>{
			try{
				call();
			}
			catch( Web::Client::ClientHttpResException& e ){
				return e.Status();
			}
			catch( const std::exception& ){
			}
			return {};
		}
	};
}