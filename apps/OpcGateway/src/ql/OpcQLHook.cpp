#include "OpcQLHook.h"
#include <jde/opc/uatypes/Logger.h>
#include "../auth/UM.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"


#define let const auto

namespace Jde::Opc::Gateway{
	using Jde::QL::Hook::Operation;

	struct HookAwait final: TAwait<jvalue>{
		HookAwait( const QL::MutationQL& m, UserPK executer, Operation op, SL sl )ι: TAwait<jvalue>{sl}, _mutation{m}, _executer{executer}, _op{op}{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<vector<ServerCnnctn>>::Task;
		α Fix( DB::Key id )ι->TAwait<Access::ProviderPK>::Task;
		const QL::MutationQL& _mutation;
		UserPK _executer;
		Operation _op;
	};

	α HookAwait::Execute()ι->ServerCnnctnAwait::Task{
		try{
			DB::Key id = (_op & Operation::Purge)==Operation::Purge
				? DB::Key{ _mutation.Id<ServerCnnctnPK>() }
				: DB::Key{ Json::AsString(_mutation.Args, "slug") };
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
			Fix( move(id) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α HookAwait::Fix( DB::Key id )ι->ProviderMAwait::Task{
		try{
			let insert = _op==(Operation::Insert | Operation::Before) || _op==(Operation::Purge | Operation::Failure);
			co_await ProviderMAwait( move(id), insert );
			ResumeScaler( {{"rowCount", 1}} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//reviews/install-issues.md #48:  " eng-test" was accepted as a slug, into the login prefix, the certificate file name and the
	//url.  Not updateable (opcGateway-meta), so the insert is the one door;  the rule itself is ServerCnnctn::SlugError, which
	//testConnection applies to a slug that is not saved yet.
	Ω slugError( const QL::MutationQL& m, SL sl )ι->up<Exception>{
		let slug = Json::FindString( m.Args, "slug" );
		return slug ? ServerCnnctn::SlugError( *slug, sl ) : nullptr;//the column is NOT NULL - the insert says so
	}
	α OpcQLHook::InsertBefore( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		if( m.TableName()!="server_connections" /*&& m.TableName()!="opc_clients"*/ )
			return nullptr;
		if( auto e = slugError(m, sl); e )
			return mu<ExceptionAwait<jvalue>>( move(e) );
		return mu<HookAwait>( m, userPK, Operation::Insert | Operation::Before, sl );
	}
	α OpcQLHook::InsertFailure( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" /*|| m.TableName()=="opc_clients"*/ ? mu<HookAwait>( m, userPK, Operation::Insert | Operation::Failure, sl ) : nullptr;
	}
	α OpcQLHook::PurgeBefore( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" /*|| m.TableName()=="opc_clients"*/ ? mu<HookAwait>( m, userPK, Operation::Purge | Operation::Before, sl ) : nullptr;
	}
	α OpcQLHook::PurgeFailure( const QL::MutationQL& m, UserPK userPK, SL sl )ι->HookResult{
		return m.TableName()=="server_connections" /*|| m.TableName()=="opc_clients"*/ ? mu<HookAwait>( m, userPK, Operation::Purge | Operation::Failure, sl ) : nullptr;
	}

	//reviews/m3-closing.md #4:  a live client holds the copy of its row it was built from.  What it reads from the copy that
	//changes how it connects or translates a path - url, certificateUri, defaultBrowseNs - takes its clients off the registry, to
	//reconnect on the new row;  a delete or purge closes them.  Name and description it only reports (search), and a rename is
	//not worth dropping every session's subscriptions for.  A restore has no clients to act on:  a deleted row cannot connect.
	Ω connectionEdited( const QL::MutationQL& m )ι->QL::IQLHook::HookResult{
		using enum QL::EMutationQL;
		let removed = m.Type==Delete || m.Type==Purge;
		let edited = m.Type==Update && ( m.Args.contains("url") || m.Args.contains("certificateUri") || m.Args.contains("defaultBrowseNs") );
		if( m.TableName()=="server_connections" && (removed || edited) ){
			if( let key = m.FindKey(); key )
				UAClient::ConnectionEdited( *key, removed );
		}
		return nullptr;
	}
	α OpcQLHook::UpdateAfter( const QL::MutationQL& m, UserPK, SL )ι->HookResult{ return connectionEdited( m ); }
	α OpcQLHook::PurgeAfter( const QL::MutationQL& m, UserPK, SL )ι->HookResult{ return connectionEdited( m ); }
}