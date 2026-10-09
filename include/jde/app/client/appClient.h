#pragma once
#include <jde/fwk/co/Timer.h>
#include <jde/web/Jwt.h>
#include <jde/app/proto/App.FromServer.pb.h>
#include <jde/app/client/usings.h>

namespace Jde::QL{ struct IQL; }
namespace Jde::Access{ struct IAcl; struct Authorize;}
namespace Jde::DB{ struct IDataSource; struct AppSchema; }
namespace Jde::App::Client{
	struct IAppClient;
	//The /server/* settings - where this process's AppServer is.  Not the live session's IsSsl()/Host(), which describe its stream.
	namespace ServerSettings{
		α IsSsl()ι->bool;
		α Host()ι->string;
		α Port()ι->PortType;
	}
	α InstanceName()ι->string;//settings "/instanceName", else Debug/Release - the name the AppServer registers this process under

	struct ConnectAwait final : VoidAwait{
		ConnectAwait( sp<IAppClient> appClient, bool retry, SRCE )ι;
	private:
		α Suspend()ι->void{ Execute(); }
		//Logs in over http, opens the socket and handshakes.  With _retry a failure is logged, /server/reconnectWait is waited
		//out from the attempt's start, and the whole attempt runs again.
		α Execute()ι->VoidAwait::Task;

		sp<IAppClient> _appClient;
		bool _retry;
	};
	α Connect( sp<IAppClient> appClient )ι->ConnectAwait::Task;
}