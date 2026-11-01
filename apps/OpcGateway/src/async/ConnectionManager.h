#pragma once
#include <open62541/client.h>
#include <open62541/plugin/eventloop.h>

namespace Jde::Opc::Gateway{
	struct Connection;
	//What the manager runs once it has handed the loop an event - the processing loop's wake-up (UAClient::Create).  Shared with
	//each connection, which runs it from the net thread, where the loop may already have freed the manager.
	using ConnectionWake = sp<const function<void()>>;

	//open62541's "tcp" UA_ConnectionManager on boost::asio, registered on the client's stock POSIX UA_EventLoop in place of
	//UA_ConnectionManager_new_POSIX_TCP (#526).  The stock one gives the loop a socket to poll, so the processing loop had to call
	//run_iterate on a timer to find out whether anything arrived.  Here the socket belongs to asio:  a connection's resolve, connect,
	//reads and writes run on one net thread of their own, and each event - established, a chunk of data, closing - is handed to the
	//loop as a UA_DelayedCallback (el->addDelayedCallback is lock-free, el->cancel ends a blocking run), so the client's callbacks
	//still run on the thread inside el->run, as open62541 requires, and the processing loop can wait for `wake` instead of polling.
	//One event is in flight per connection:  the next read is armed only once the loop has taken the last chunk, so the receive
	//buffer is reused and a flood of bytes is paced by the client's parsing.  An event is bytes, not a call:  a request is written
	//and then nothing of it lives here, its answer is a chunk like any other, and one chunk may carry several;  a call the server
	//takes minutes over holds nothing, and the other calls' answers are delivered as they land.  The hold lasts one run_iterate.
	//Not the executor's pool:  the client's synchronous calls (disconnect, delete, the tests' sync services) spin el->run inside a
	//strand handler on a pool thread, and with every pool thread in one - Shutdown stops every client at once - nothing could
	//produce the close those loops wait for.
	//The contract (eventloop.h, ua_client_connect.c):  openConnection delivers OPENING before it returns;  CLOSING comes from a later
	//run, once, last, never from inside closeConnection (UA_SecureChannel_shutdown writes its state after that returns);  a
	//connection id is never 0;  sendWithConnection owns its buffer, also when it fails;  a buffer is allocNetworkBuffer's.
	struct AsioConnectionManager final : UA_ConnectionManager{
		//Heap-allocated;  the loop frees it with the client (eventSource.free).
		Ω Create( Jde::Handle client, ConnectionWake wake )ε->UA_ConnectionManager*;
		struct Counts final{ uint Opened{}; uint Established{}; uint Closings{}; };
		Ω Stats( UA_Client* ua )ι->optional<Counts>;//the client's manager's, where it is this one - for tests.
	private:
		AsioConnectionManager( Jde::Handle client, ConnectionWake wake )ε;
		~AsioConnectionManager();
		Ω Self( UA_ConnectionManager* cm )ι->AsioConnectionManager&{ return *static_cast<AsioConnectionManager*>(cm); }
		Ω Self( UA_EventSource* es )ι->AsioConnectionManager&{ return Self( (UA_ConnectionManager*)es ); }
		//The function pointers UA_ConnectionManager carries.  All on the thread inside el->run, or under the client's lock.
		Ω OpenConnection( UA_ConnectionManager* cm, const UA_KeyValueMap* params, void* application, void* context, UA_ConnectionManager_connectionCallback callback )ι->UA_StatusCode;
		Ω SendWithConnection( UA_ConnectionManager* cm, uintptr_t connectionId, const UA_KeyValueMap* params, UA_ByteString* buf )ι->UA_StatusCode;
		Ω CloseConnection( UA_ConnectionManager* cm, uintptr_t connectionId )ι->UA_StatusCode;
		Ω AllocNetworkBuffer( UA_ConnectionManager* cm, uintptr_t connectionId, UA_ByteString* buf, size_t bufSize )ι->UA_StatusCode;
		Ω FreeNetworkBuffer( UA_ConnectionManager* cm, uintptr_t connectionId, UA_ByteString* buf )ι->void;
		Ω Start( UA_EventSource* es )ι->UA_StatusCode;
		Ω Stop( UA_EventSource* es )ι->void;
		Ω Free( UA_EventSource* es )ι->UA_StatusCode;
		Ω Deliver( void* application, void* context )ι->void;//the delayed callback:  `context` is the connection's id.

		α Find( uintptr_t id )ι->sp<Connection>;
		α Erase( uintptr_t id )ι->void;
		α CheckStopped()ι->void;//STOPPING with no connection left is STOPPED - the loop's checkClosed reads it.
		α LogPrefix()Ι->string;

		const Jde::Handle _client;
		const ConnectionWake _wake;
		sp<struct NetThread> _net;
		//The connections by id, and the stopping transition.  Taken on the loop thread alone - the net thread holds its own sp<Connection>s -
		//after the loop's own mutex, and never across the client's callback:  that reopens and closes connections from inside it.
		std::mutex _mutex;
		flat_map<uintptr_t,sp<Connection>> _connections;
		uintptr_t _nextId{ 1 };
		std::atomic<uint> _opened{}, _established{}, _closings{};
		friend struct Connection;
		constexpr static ELogTags _tags{ (ELogTags)EOpcLogTags::Net };
	};
}