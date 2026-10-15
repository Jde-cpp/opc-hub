#pragma once
#ifndef OPC_CLIENT
#define OPC_CLIENT
#include <jde/fwk/co/Await.h>
#include <jde/db/Row.h>
#include <jde/db/Key.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/opc/usings.h>

namespace Jde::Opc::Gateway{
	struct ServerCnnctn final{
		ServerCnnctn( str address )ι:Url{address}{}
		ServerCnnctn( DB::Row&& r )ε;
		ServerCnnctn( jobject&& o )ε;
		α ToJson()Ι->jobject;
		//reviews/install-issues.md #48:  the slug is the `slug\user` login's prefix, a certificate's file name (OpcHub.<slug>.pem) and a
		//url segment - the one rule the insert (OpcQLHook) and testConnection apply;  the web form checks the same (ServerCnnctn.slugPattern).
		//Null for a valid slug.
		Ω SlugError( sv slug, SL sl )ι->up<Exception>;
		ServerCnnctnPK Id;
		string Url;
		string CertificateUri;
		NsIndex DefaultBrowseNs{1};
		string Description;
		bool IsDefault{};
		string Name;
		optional<TimePoint> Deleted;
		ServerCnnctnNK Slug;
	};

	struct ServerCnnctnAwait final: TAwait<vector<ServerCnnctn>>{
		using base=TAwait<vector<ServerCnnctn>>;
		ServerCnnctnAwait( optional<DB::Key> key=nullopt, bool includeDeleted=false):_key{key}, _includeDeleted{includeDeleted}{};
		α Suspend()ι->void override{ Select(); }
		α Select()ι->DB::SelectAwait::Task;
	private:
		optional<DB::Key> _key;
		bool _includeDeleted;
	};
}
#endif