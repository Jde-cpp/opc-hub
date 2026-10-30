#include "OpcQLHook.h"
#include <jde/opc/uatypes/Logger.h>
#include "../auth/UM.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"
#include "../server/GatewayServer.h"


#define let const auto

namespace Jde::Opc::Gateway{
	using Jde::QL::Hook::Operation;

	struct HookAwait final: TAwaitEx<jvalue,ServerCnnctnAwait::Task>{
		HookAwait( const QL::MutationQL& m, UserPK executer, Operation op, SL sl )ι: TAwaitEx{sl}, _mutation{m}, _executer{executer}, _op{op}{}
	private:
		α Execute()ι->ServerCnnctnAwait::Task override;//its own type is the select's;  the provider row goes through Any().
		const QL::MutationQL& _mutation;
		UserPK _executer;
		Operation _op;
	};

	α HookAwait::Execute()ι->ServerCnnctnAwait::Task{
		try{
			DB::Key id = (_op & Operation::Purge)==Operation::Purge
				? DB::Key{ _mutation.Id<ServerCnnctnPK>() }
				: DB::Key{ string{_mutation.As<jstring>("slug")} };
			//reviews/install-issues.md #51:  the insert failed on the slug's unique index - a delete is soft, so a deleted row keeps its
			//slug, and its provider, which InsertBefore reused:  purging it would leave the deleted row nothing to restore to.  The
			//engine's duplicate names the schema and is withheld from the client, so this refusal replaces it (InsertAwait).
			if( _op==(Operation::Insert | Operation::Failure) ){
				if( let existing = co_await ServerCnnctnAwait{ id, true }; existing.size() ){
					let& slug = existing.front().Slug;
					ResumeExp( Exception{existing.front().Deleted
						? Ƒ( "Slug '{}' belongs to a deleted connection:  restore it, or purge it to reuse the slug.", slug )
						: Ƒ( "Slug '{}' is in use by another connection.", slug ),
						ExceptionArgs{EHttpStatus::BadRequest}, _sl} );
					co_return;
				}
			}
			ProviderMAwait provider{ move(id), _op==(Operation::Insert | Operation::Before) || _op==(Operation::Purge | Operation::Failure) };
			co_await Any( provider );
			ResumeScaler( {{"rowCount", 1}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//reviews/install-issues.md #48:  " eng-test" was accepted as a slug, into the login prefix, the certificate file name and the
	//url.  Not updateable (opcGateway-meta), so the insert is the one door;  the rule itself is ServerCnnctn::SlugError, which
	//testConnection applies to a slug that is not saved yet.  The hooks here read the mutation through Input's Find/As, never Args:
	//a value bound through a variable is its `\b$name` marker there (reviews-issue/servers-2-endpoint.md #7).
	Ω slugError( const QL::MutationQL& m, SL sl )ι->up<Exception>{
		let slug = m.FindPtr<jstring>( "slug" );
		return slug ? ServerCnnctn::SlugError( sv{*slug}, sl ) : nullptr;//the column is NOT NULL - the insert says so
	}
	//The columns the connection settings' save rules read (spec *Connection settings*), by their QL names, and with the url and the
	//default namespace every column a live client was built from - an edit to any of these reconnects its clients (connectionEdited).
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
	//The save rules on an update (spec *Connection settings*):  an update carries only its changed columns, and certificate_uri is
	//updateable where slug is not, so a Sign row losing its uri, or a mode pinned on a row that has none, shows only with the saved
	//row under the edit.  Its own await:  the select's task type, as HookAwait.
	struct SettingsAwait final: TAwaitEx<jvalue,ServerCnnctnAwait::Task>{
		SettingsAwait( const QL::MutationQL& m, SL sl )ι: TAwaitEx{sl}, _mutation{m}{}
	private:
		α Execute()ι->ServerCnnctnAwait::Task override;
		const QL::MutationQL& _mutation;
	};
	α SettingsAwait::Execute()ι->ServerCnnctnAwait::Task{
		try{
			auto key = _mutation.GetKey( _sl );
			auto rows = co_await ServerCnnctnAwait{ key, true };
			THROW_IFX( rows.empty(), Exception(_sl, {EHttpStatus::NotFound}, "The connection to update was not found:  {}.", _mutation.ToString()) );
			if( auto e = settingsError(_mutation, move(rows.front()), _sl); e )
				e->Throw();
			ResumeScaler( {{"rowCount", 1}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α OpcQLHook::InsertBefore( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		if( m.TableName()!="server_connections" )
			return nullptr;
		if( auto e = slugError(m, sl); e )
			return mu<ExceptionAwait<jvalue>>( move(e) );
		if( auto e = settingsError(m, ServerCnnctn{string{}}, sl); e )//over a blank row:  an insert carries every column it sets.
			return mu<ExceptionAwait<jvalue>>( move(e) );
		return mu<HookAwait>( m, userPK, Operation::Insert | Operation::Before, sl );
	}
	α OpcQLHook::UpdateBefore( const QL::MutationQL& m, UserPK, SL sl )ι->HookResult{
		let settings = m.TableName()=="server_connections" && m.Type==QL::EMutationQL::Update && namesAny( m, _settingsColumns );//a delete or restore changes nothing the rules read.
		return settings ? mu<SettingsAwait>( m, sl ) : nullptr;
	}
	//The endpoint's folder for the row just written (spec *Address space*):  the row's slug and url are the insert's own args, so
	//no select;  the id is the insert's.  Nothing to await - the server posts the add to the executor.
	α OpcQLHook::InsertAfter( const QL::MutationQL& m, UserPK, uint id, SL )ι->HookResult{
		if( m.TableName()=="server_connections" ){
			let slug = m.FindPtr<jstring>( "slug" );
			if( auto server = GatewayServer::Instance(); server && slug ){
				let url = m.FindPtr<jstring>( "url" );
				ServerCnnctn row{ url ? string{*url} : string{} };
				row.Id = (ServerCnnctnPK)id;
				row.Slug = string{ *slug };
				server->ConnectionCreated( move(row) );
			}
		}
		return nullptr;
	}
	α OpcQLHook::InsertFailure( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" ? mu<HookAwait>( m, userPK, Operation::Insert | Operation::Failure, sl ) : nullptr;
	}
	α OpcQLHook::PurgeBefore( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" ? mu<HookAwait>( m, userPK, Operation::Purge | Operation::Before, sl ) : nullptr;
	}
	α OpcQLHook::PurgeFailure( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" ? mu<HookAwait>( m, userPK, Operation::Purge | Operation::Failure, sl ) : nullptr;
	}

	//reviews/m3-closing.md #4:  a live client holds the copy of its row it was built from.  What it reads from the copy that
	//changes how it connects or translates a path - url, certificateUri, defaultBrowseNs, and the spec's connection settings - takes
	//its clients off the registry, to reconnect on the new row;  a delete or purge closes them.  Name and description it only reports
	//(search), and a rename is not worth dropping every session's subscriptions for.  A restore has no clients to act on:  a deleted
	//row cannot connect.  The endpoint hears the same edits (server/GatewayServer.h):  a purge takes the row's folder, a delete leaves
	//it, and a url edit is the one column its `_Connection` shows.
	Ω connectionEdited( const QL::MutationQL& m )ι->QL::IQLHook::HookResult{
		using enum QL::EMutationQL;
		constexpr array<sv,2> pathColumns{ "url", "defaultBrowseNs" };
		let removed = m.Type==Delete || m.Type==Purge;
		let edited = m.Type==Update && ( namesAny(m, pathColumns) || namesAny(m, _settingsColumns) );
		if( m.TableName()=="server_connections" && (removed || edited) ){
			if( let key = m.FindKey(); key ){
				UAClient::ConnectionEdited( *key, removed );
				if( auto server = GatewayServer::Instance(); server ){
					if( m.Type==Purge )
						server->ConnectionPurged( *key );
					else if( let url = m.FindPtr<jstring>("url"); url && m.Type==Update )
						server->ConnectionUrlChanged( *key, string{*url} );
				}
			}
		}
		return nullptr;
	}
	α OpcQLHook::UpdateAfter( const QL::MutationQL& m, UserPK, SL )ι->HookResult{ return connectionEdited( m ); }
	α OpcQLHook::PurgeAfter( const QL::MutationQL& m, UserPK, SL )ι->HookResult{ return connectionEdited( m ); }
}
namespace Jde::Opc{
	α Gateway::MergeSettings( const QL::Input& input, ServerCnnctn base, SL sl )ε->ServerCnnctn{
		let text = [&]( sv name, string& into ){
			if( let v = input.FindPtr<jvalue>(name); v )
				into = v->is_string() ? string{ Str::Trim(sv{v->get_string()}) } : string{};
		};
		let number = [&]( sv name, optional<uint32>& into ){
			let v = input.FindPtr<jvalue>( name );
			if( !v )
				return;
			if( v->is_null() ){
				into.reset();
				return;
			}
			let n = v->try_to_number<uint32>();
			THROW_IFX( !n, Exception(sl, {EHttpStatus::BadRequest}, "'{}' takes a whole number of milliseconds, not {}.", name, serialize(*v)) );
			into = *n;
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