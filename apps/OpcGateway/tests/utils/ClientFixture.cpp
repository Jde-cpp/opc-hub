#include "ClientFixture.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ ELogTags::Test };

	α Tests::TryConnect( const ServerCnnctnNK& slug, Credential cred )ι->std::expected<sp<UAClient>,up<Exception>>{
		try{
			return BlockTAwait<sp<UAClient>>( UAClient::GetClient(slug, move(cred)) );
		}
		catch( Exception& e ){
			return std::unexpected{ e.Move() };
		}
	}
	α Tests::Connect( ServerCnnctnNK slug, Credential cred, sp<UAClient>* client, up<Exception>* error, atomic_flag* done )ι->ConnectAwait::Task{
		try{
			*client = co_await UAClient::GetClient( move(slug), move(cred) );
		}
		catch( Exception& e ){
			*error = e.Move();
		}
		done->test_and_set();
		done->notify_all();
	}

	α Tests::ClientFixture::TearDown()ι->void{
		if( _client )
			UAClient::RemoveClient( move(_client) );
	}
	α Tests::ClientFixture::Attempt( const ServerCnnctnNK& slug, Credential cred, sv cell )ι->string{
		_exception = nullptr;
		if( auto client = TryConnect(slug, move(cred)); client )
			_client = move( *client );
		else
			_exception = move( client.error() );
		auto what = _exception ? string{ _exception->what() } : string{};
		if( cell.size() )
			INFO( "MATRIX|{}|{}", cell, what.empty() ? Negotiated(_client) : what );
		return what;
	}
}