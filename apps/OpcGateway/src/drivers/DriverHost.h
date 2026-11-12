#pragma once
#include <jde/db/Key.h>
#include <jde/opc/server/UAAccess.h>
#include <jde/opc/server/UAServer.h>
#include "IDriver.h"
#include "../types/ServerCnnctn.h"

namespace Jde::Opc::Gateway{
	//A driver row's UA server and the driver behind it (docs/open-protocol/spec.md *The driver server*):  one per row of a kind
	//that registers an IDriver, whatever the kind.  The server is the library's over DriverServer::Settings, on loopback at
	///gateway/drivers/portBase plus the row's id, on a thread of its own;  its sessions are UAAccess's over the gateway's
	//appClient, and its nodes are open to browse and read by every session and writable by none.  The row's one namespace is
	//urn:jde:<slug>:<kind>, index 2, where its driver builds.
	//The hosts start with the gateway, after the access snapshot and before the groups load, whose first subscribes find them
	//listening, and stop with it, after its clients.  Between the two they follow their rows, told by ServerCnnctnMAwait once a row
	//is written:  a row made or restored gets its host, an edit of its url, kind or `driver` stops the host and starts one on the
	//row as it now is, and a delete or a purge stops it.  Every start and stop after the gateway's own runs on one thread of the
	//hosts', in the order the rows changed, so none waits on an executor thread:  a stop waits for the driver's link to close,
	//which the executor's strand does, and for the sessions its clients are closing.
	struct DriverHost final{
		//The row's server listening and its driver started.  Throws, naming the port and portBase, where it cannot:  the port
		//another holds, the row's own refusal.
		DriverHost( const ServerCnnctn& row, SRCE )ε;
		~DriverHost();
		DriverHost( const DriverHost& )=delete;
		α operator=( const DriverHost& )->DriverHost& =delete;

		//Gateway::Connect's:  a host for each row there is with a driver.  One that cannot start is the start-up's error.
		Ω Start()ε->void;
		//The shutdown's, after UAClient::Shutdown's:  every driver stops, then, without `terminate`, the sessions the clients
		//are closing are waited for, then the servers go.
		Ω Shutdown( bool terminate )ι->void;
		//ServerCnnctnMAwait's, once `row` is written:  its host stops, and, not `removed`, the row is read and one starts where it
		//stands with a driver's kind.  A host that cannot start is logged, naming the port and portBase, and the row stands:
		//its clients try again as against any server that is not there.  Nothing before Start, whose read has the row.
		Ω RowChanged( DB::Key row, bool removed )ι->void;
		Ω Find( const ServerCnnctnNK& slug )ι->sp<DriverHost>;//the slug's running host, or null.
		Ω NamespaceUri( const ServerCnnctnNK& slug, sv kind )ι->string{ return Ƒ( "urn:jde:{}:{}", slug, kind ); }

		α Id()Ι->ServerCnnctnPK{ return _id; }
		α Slug()Ι->const ServerCnnctnNK&{ return _slug; }
		α Port()Ι->PortType{ return _port; }
		α Sessions()Ι->vector<Opc::Server::UAAccess::Session>{ return _sessions->List(); }//the sessions its server holds, none once it has stopped.
	private:
		//The drivers stop, each as its protocol closes its link;  then, with `wait`, the sessions open as the stop began are waited
		//for, which their clients are closing;  then the servers go.
		Ω Stop( std::span<const sp<DriverHost>> hosts, bool wait )ι->void;
		const ServerCnnctnPK _id;
		const ServerCnnctnNK _slug;
		PortType _port{};
		up<Opc::Server::UAServer> _server;//before _driver, which goes first.
		up<IDriver> _driver;
		sp<Opc::Server::UAAccess::SessionList> _sessions;
		friend struct DriverHosts;
	};
}