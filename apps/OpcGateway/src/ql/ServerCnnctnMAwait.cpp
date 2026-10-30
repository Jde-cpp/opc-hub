#include "ServerCnnctnMAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/ql/ops/MutationAwait.h>
#include "../auth/UM.h"
#include "../GatewayAppClient.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"
#include "../server/GatewayServer.h"

#define let const auto

namespace Jde::Opc::Gateway{
	using QL::EMutationQL;

	struct ProviderStepAwait final: TAwaitEx<jvalue,ServerCnnctnAwait::Task>{
		ProviderStepAwait( const QL::MutationQL& m, EProviderStep step, SL sl )ι: TAwaitEx{sl}, _mutation{m}, _step{step}{}
	private:
		α Execute()ι->ServerCnnctnAwait::Task override;//its own type is the select's;  the provider row goes through Any().
		const QL::MutationQL& _mutation;
		EProviderStep _step;
	};

	α ProviderStepAwait::Execute()ι->ServerCnnctnAwait::Task{
		using enum EProviderStep;
		try{
			DB::Key id = _step==PurgeBefore || _step==PurgeFailure
				? DB::Key{ _mutation.Id<ServerCnnctnPK>() }
				: DB::Key{ string{_mutation.As<jstring>("slug")} };
			//reviews/install-issues.md #51:  the insert failed on the slug's unique index - a delete is soft, so a deleted row keeps its
			//slug, and its provider, which the create's step reused:  purging it would leave the deleted row nothing to restore to.  The
			//engine's duplicate names the schema and is withheld from the client, so this refusal replaces it (ServerCnnctnMAwait).
			if( _step==InsertFailure ){
				if( let existing = co_await ServerCnnctnAwait{ id, true }; existing.size() ){
					let& slug = existing.front().Slug;
					ResumeExp( Exception{existing.front().Deleted
						? Ƒ( "Slug '{}' belongs to a deleted connection:  restore it, or purge it to reuse the slug.", slug )
						: Ƒ( "Slug '{}' is in use by another connection.", slug ),
						ExceptionArgs{EHttpStatus::BadRequest}, _sl} );
					co_return;
				}
			}
			ProviderMAwait provider{ move(id), _step==InsertBefore || _step==PurgeFailure };
			co_await Any( provider );
			ResumeScaler( {{"rowCount", 1}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ServerCnnctnMAwait::ProviderStep( const QL::MutationQL& m, EProviderStep step, SL sl )ι->up<TAwait<jvalue>>{
		return mu<ProviderStepAwait>( m, step, sl );
	}

	//reviews/install-issues.md #48:  " eng-test" was accepted as a slug, into the login prefix, the certificate file name and the
	//url.  Not updateable (opcGateway-meta), so the insert is the one door;  the rule itself is ServerCnnctn::SlugError, which
	//testConnection applies to a slug that is not saved yet.  The rules here read the mutation through Input's Find/As, never Args:
	//a value bound through a variable is its `\b$name` marker there (reviews-issue/servers-2-endpoint.md #7).
	Ω slugError( const QL::MutationQL& m, SL sl )ι->up<Exception>{
		let slug = m.FindPtr<jstring>( "slug" );
		return slug ? ServerCnnctn::SlugError( sv{*slug}, sl ) : nullptr;//the column is NOT NULL - the insert says so
	}
	//The columns the connection settings' save rules read (spec *Connection settings*), by their QL names.
	constexpr array<sv,8> _settingsColumns{ "certificateUri", "securityMode", "securityPolicy", "sessionTimeout", "requestTimeout", "pingInterval", "ttl", "locale" };
	Ω namesAny( const QL::MutationQL& m, std::span<const sv> columns )ι->bool{ return std::ranges::any_of( columns, [&m]( sv c ){ return m.Args.contains(c); } ); }
	//ServerCnnctn::SettingsError over the mutation merged onto `base`:  a refusal, MergeSettings' own included, else null.
	Ω settingsError( const QL::MutationQL& m, ServerCnnctn base, SL sl )ι->up<Exception>{
		try{
			return ServerCnnctn::SettingsError( MergeSettings(m, move(base), sl), sl );
		}
		catch( Exception& e ){
			return e.Move();
		}
	}
	//The endpoint's folder for the row just written (spec *Address space*):  the row's slug and url are the insert's own args, so
	//no select;  the id is the insert's.  Nothing to await - the server posts the add to the executor.
	Ω connectionCreated( const QL::MutationQL& m, const jvalue& result )ι->void{
		let row0 = result.is_array() && result.get_array().size() ? result.get_array()[0].if_object() : nullptr;
		//The object overload:  the jvalue one takes a json pointer path, which "id" is not (#223).
		let id = row0 ? Json::FindNumber<uint>( *row0, "id" ).value_or( 0 ) : 0;
		let slug = m.FindPtr<jstring>( "slug" );
		if( auto server = GatewayServer::Instance(); server && slug ){
			let url = m.FindPtr<jstring>( "url" );
			ServerCnnctn row{ url ? string{*url} : string{} };
			row.Id = (ServerCnnctnPK)id;
			row.Slug = string{ *slug };
			server->ConnectionCreated( move(row) );
		}
	}

	//A duration an update names:  its value, or an empty optional where it clears it;  nullopt where the update leaves it.  The save
	//has checked it (MergeSettings).
	Ω namedDuration( const QL::MutationQL& m, sv name )ι->optional<optional<uint32>>{
		let v = m.FindPtr<jvalue>( name );
		if( !v )
			return nullopt;
		optional<uint32> y;
		if( let n = v->try_to_number<uint>(); n )
			y = (uint32)*n;
		return std::make_optional( y );
	}
	//reviews/m3-closing.md #4:  a live client holds the copy of its row it was built from.  What it reads from the copy that
	//changes how it connects or translates a path - url, certificateUri, defaultBrowseNs, and the spec's connection settings - takes
	//its clients off the registry, to reconnect on the new row;  a delete or purge closes them.  The ping interval and TTL it reads
	//live, so an edit to them alone is written over its copy instead.  Name and description it only reports (search), and a rename
	//is not worth dropping every session's subscriptions for.  A restore has no clients to act on:  a deleted row cannot connect.
	//The endpoint hears the same edits (server/GatewayServer.h):  a purge takes the row's folder, a delete leaves it, and a url
	//edit is the one column its `_Connection` shows.
	Ω connectionEdited( const QL::MutationQL& m )ι->void{
		using enum EMutationQL;
		constexpr array<sv,8> builtColumns{ "url", "defaultBrowseNs", "certificateUri", "securityMode", "securityPolicy", "sessionTimeout", "requestTimeout", "locale" };
		constexpr array<sv,2> liveColumns{ "pingInterval", "ttl" };
		let removed = m.Type==Delete || m.Type==Purge;
		let edited = m.Type==Update && namesAny( m, builtColumns );
		let live = m.Type==Update && !edited && namesAny( m, liveColumns );//with a reconnect, the new client reads them from the row.
		if( !removed && !edited && !live )
			return;
		let key = m.FindKey();
		if( !key )
			return;
		if( live ){
			UAClient::KeepAliveEdited( *key, namedDuration(m, "pingInterval"), namedDuration(m, "ttl") );
			return;
		}
		UAClient::ConnectionEdited( *key, removed );
		if( auto server = GatewayServer::Instance(); server ){
			if( m.Type==Purge )
				server->ConnectionPurged( *key );
			else if( let url = m.FindPtr<jstring>("url"); url && m.Type==Update )
				server->ConnectionUrlChanged( *key, string{*url} );
		}
	}

	α ServerCnnctnMAwait::IsApplicable( const QL::MutationQL& m )ι->bool{
		using enum EMutationQL;
		return m.TableName()=="server_connections" && (m.Type==Create || m.Type==Update || m.Type==Delete || m.Type==Restore || m.Type==Purge);
	}
	//Not inside the schema's sync:  the seed's row (release-opcServer.mutation) is applied there, in the hub too, and the access
	//seed ships that row's provider, so the stock insert is enough.  A provider step there would make the hub's start hang on the
	//access seed running first (qlhook-removal #4).  Nor before the gateway's login:  the provider row lives on the AppServer.
	//The hub's client is in-process and always connected.
	static std::atomic<bool> _synced;
	α ServerCnnctnMAwait::SetSynced()ι->void{ _synced = true; }
	α ServerCnnctnMAwait::Test( QL::MutationQL& m, QL::Creds executer, SL sl )->up<TAwait<jvalue>>{
		return IsApplicable( m ) && _synced && AppClient()->Connected() ? mu<ServerCnnctnMAwait>( move(m), move(executer), sl ) : nullptr;
	}
	//One coroutine:  the provider steps and the row select are the select's task type and go through Any(), the stock op is this
	//frame's own.  A create's or a purge's stock op runs between its two provider steps:  a db failure is parked, the failure step
	//awaited *after* the handler - co_await is not allowed inside one - then rethrown through the up<runtime_error>, whose dynamic
	//type ResumeExp keeps (ql-review3 #28:  a duplicate key stays a DBException, and a 409).
	α ServerCnnctnMAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			using enum EMutationQL;
			using enum EProviderStep;
			let type = _query.Type;
			_query.DBTable->Authorize( QL::StockRights(type), UserPK(), _sl );//asked first (ql-review3 #25):  the provider step runs before the statement.
			optional<std::pair<EProviderStep,EProviderStep>> steps;//before the stock op, and on its failure.
			if( type==Create ){
				QL::CheckCreate( _query, UserPK(), _sl );//the engine's refusals before the provider step, which would reuse a provider and then purge it.
				if( auto e = slugError(_query, _sl); e )
					e->Throw();
				if( auto e = settingsError(_query, ServerCnnctn{string{}}, _sl); e )//over a blank row:  an insert carries every column it sets.
					e->Throw();
				steps = { InsertBefore, InsertFailure };
			}
			else if( type==Purge )
				steps = { PurgeBefore, PurgeFailure };
			else if( type==Update && namesAny(_query, _settingsColumns) ){
				//The save rules on an update (spec *Connection settings*):  an update carries only its changed columns, and certificate_uri is
				//updateable where slug is not, so a Sign row losing its uri, or a mode pinned on a row that has none, shows only with the saved
				//row under the edit.  A delete or restore changes nothing the rules read.  Two updates that each pass can still cross - the
				//check is on what was read, the statement runs after - so the cross-column half is a before-update trigger too (#520), which
				//refuses in the statement, as the app's own error.
				ServerCnnctnAwait select{ _query.GetKey(_sl), true };
				auto rows = co_await Any( select );
				THROW_IFX( rows.empty(), Exception(_sl, {EHttpStatus::NotFound}, "The connection to update was not found:  {}.", _query.ToString()) );
				if( auto e = settingsError(_query, move(rows.front()), _sl); e )
					e->Throw();
			}
			if( steps ){
				ProviderStepAwait before{ _query, steps->first, _sl };
				co_await Any( before );
			}
			jvalue y; up<runtime_error> failure;
			try{
				y = co_await QL::MutationAwait{ QL::MutationQL{_query}, _creds, nullptr, _sl };//the stock op:  no QL, so nothing routes it back here.
			}
			catch( runtime_error& e ){
				if( !steps )
					throw;
				failure = ToExceptionPtr( move(e) );
			}
			if( failure ){
				ProviderStepAwait step{ _query, steps->second, _sl };
				try{
					co_await Any( step );//the db error is the one reported;  a step that itself throws replaces it.
				}
				catch( runtime_error& ){
					//superseded - a create's step refuses a used slug, which says what went wrong (install-issues #51).  A purge's step
					//failing is a second fault, and its db error keeps its level.
					if( auto p = type==Create ? dynamic_cast<Exception*>(failure.get()) : nullptr; p )
						p->SetLevel( ELogLevel::Debug );
					throw;
				}
				ResumeExp( move(*failure) );
			}
			else{
				if( type==Create )
					connectionCreated( _query, y );
				else
					connectionEdited( _query );
				Resume( move(y) );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
namespace Jde::Opc{
	α Gateway::MergeSettings( const QL::Input& input, ServerCnnctn base, SL sl )ε->ServerCnnctn{
		let text = [&]( sv name, string& into ){
			if( let v = input.FindPtr<jvalue>(name); v )
				into = v->is_string() ? string{ v->get_string() } : string{};//untrimmed:  the rules judge what the engine stores.
		};
		let number = [&]( sv name, optional<uint32>& into ){
			let v = input.FindPtr<jvalue>( name );
			if( !v )
				return;
			if( v->is_null() ){
				into.reset();
				return;
			}
			let n = v->try_to_number<uint>();//uint32 is 64 bits on Linux and 32 on Windows:  the same input, the same answer.
			THROW_IFX( !n, Exception(sl, {EHttpStatus::BadRequest}, "'{}' takes a whole number of milliseconds, not {}.", name, serialize(*v)) );
			THROW_IFX( *n>ServerCnnctn::MaxDurationMs, Exception(sl, {EHttpStatus::BadRequest}, "'{}' takes at most {} milliseconds, about 24 days, not {}.", name, ServerCnnctn::MaxDurationMs, *n) );
			into = (uint32)*n;
		};
		text( "certificateUri", base.CertificateUri );
		text( "securityMode", base.SecurityMode );
		text( "securityPolicy", base.SecurityPolicy );
		number( "sessionTimeout", base.SessionTimeout );
		number( "requestTimeout", base.RequestTimeout );
		number( "pingInterval", base.PingInterval );
		number( "ttl", base.Ttl );
		text( "locale", base.Locale );
		return base;
	}
}