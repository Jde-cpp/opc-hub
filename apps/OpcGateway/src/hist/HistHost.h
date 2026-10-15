#pragma once
#include <absl/functional/any_invocable.h>
#include <jde/fwk/co/AnyAwait.h>
#include "HistGroupHost.h"

namespace Jde::Opc::Gateway{
	//The live side of a table edit, as HistGroupMAwait awaits it:  resumed once that side is done, or with its failure, which
	//the mutation then reports.
	struct HistWork final : AnyVoidAwait{
		using Work = absl::AnyInvocable<VoidTask( HistWork& )>;
		HistWork( Work work, SRCE )ι:AnyVoidAwait{ sl }, _work{ std::move(work) }{}
		α Done()ι->void{ Resume(); }
	protected:
		α Suspend()ι->void override{ std::move(_work)( *this ); }
	private:
		Work _work;
	};
	//A connection's slug, deleted or not:  what the collector's client is keyed by.  NotFound for a connection that doesn't exist.
	struct SlugAwait final : AnyAwait<string>{
		SlugAwait( ServerCnnctnPK connection, SRCE )ι:AnyAwait<string>{ sl }, _connection{ connection }{}
	protected:
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->VoidTask;
		ServerCnnctnPK _connection;
	};
	//A template member as resolveHistory answers it (spec *Templates*):  its row and path, the node it resolved to, Good once it
	//has a hist_group_nodes row, else why it hasn't, and for Bad_NodeIdExists the group that holds the node - another on the
	//connection, or this one as another member or an ad hoc node - by its row and name, so a page can say which.
	struct HistResolved final{ HistTemplateNodePK TemplateNode; string Path; optional<ExNodeId> Node; StatusCode Status; optional<HistGroupPK> Holder; string HolderName; };
	struct HistResolveAwait final : AnyAwait<vector<HistResolved>>{
		HistResolveAwait( HistGroupPK group, UserPK by, SRCE )ι:AnyAwait<vector<HistResolved>>{ sl }, Group{ group }, By{ by }{}
		const HistGroupPK Group;
		const UserPK By;
	protected:
		α Suspend()ι->void override;
	};

	//The gateway's historian (spec *Placement*, *Collection*, *Templates*):  the library over /gateway/hist, one HistGroupHost
	//per hist_groups row, loaded at start and kept in step with the tables by HistGroupMAwait, whose live side runs here.  Without
	//a hist block, or with another process holding the lock under its path, the tables still work and nothing is stored or
	//monitored, as a group's reads and edits then answer with an error (TestEnabled).
	struct HistHost final : noncopyable{
		Ω Instance()ι->HistHost&;
		//The settings, the library, the groups the tables hold and their items.  Blocks on the load:  Gateway::Connect's thread.
		α Start()ε->void;
		//Every item goes and every group's last flush runs, here or once a load or remove in flight lets go of the historian:  before
		//the UA clients go, while the executor still runs.
		α Stop()ι->void;
		α Enabled()Ι->bool{ auto historian = Historian(); return historian && historian->Enabled(); }
		//What a group's history fields answer while the gateway runs without its historian (spec *Day files*):  ServiceUnavailable,
		//saying why - no hist block, another process holding the lock, or the gateway's end.  The tables still answer.
		α TestEnabled( SRCE )Ε->void;
		α Historian()Ι->sp<Opc::Hist::Historian>{ lg _{ _mutex }; return _historian; }
		α Find( HistGroupPK id )Ι->sp<HistGroupHost>;
		α Groups()Ι->vector<sp<HistGroupHost>>;
		//The collector's credential for a connection (spec *Collector contract*):  /gateway/hist/credentials/<slug>'s user and
		//password, else the process's own certificate.
		α Collector( const ServerCnnctnNK& slug )Ι->Credential;
		α Writer( UserPK by )Ι->Opc::Hist::Writer;
		//Resolves every member of the group's template that has no hist_group_nodes row yet:  each path that resolves on the
		//group's connection, from its root, becomes a row through createHistGroupNode as `by`, whose hook joins the node.  One
		//answer per member of the template.  A server that can't be reached fails every pending member with the connect's
		//status.  A member whose node another group holds, or this one as another member or an ad hoc node, answers Bad_NodeIdExists,
		//one a concurrent resolve made the row for Good, one whose row `by` may not create Bad_UserAccessDenied, and one whose create
		//failed otherwise Bad_InternalError.  A group with no template has no members to answer.  Throws for a group that doesn't exist.
		α Resolve( HistGroupPK group, UserPK by, SRCE )ι->HistResolveAwait{ return { group, by, sl }; }

		//HistGroupMAwait's:  the live side of each table edit, after the row is written, or before it goes.
		α GroupCreated( HistGroupPK id, UserPK by, HistWork& await )ι->VoidTask;
		α GroupEdited( DB::Key key, HistWork& await )ι->VoidTask;//its threshold columns:  every member's re-resolved.
		α GroupDeleted( DB::Key key, UserPK by, HistWork& await )ι->VoidTask;//every member leaves, by the caller.
		α GroupRestored( DB::Key key, UserPK by, HistWork& await )ι->VoidTask;//its rows join again, and the members its template gained meanwhile resolve.
		α GroupPurging( HistGroupPK id, UserPK by, HistWork& await )ι->VoidTask;//as deleted, then its hist_group_nodes rows go.
		α NodeCreated( HistGroupNodePK id, UserPK by, HistWork& await )ι->VoidTask;
		α NodeEdited( HistGroupNodePK id, HistWork& await )ι->VoidTask;
		α NodePurging( HistGroupNodePK id, UserPK by, HistWork& await )ι->VoidTask;
		α TemplateNodeCreated( HistTemplateNodePK id, UserPK by, HistWork& await )ι->VoidTask;//resolved in each of the template's groups.
		α TemplateNodeEdited( HistTemplateNodePK id, HistWork& await )ι->VoidTask;//its thresholds reach each group node made from it.
		α TemplateNodePurging( HistTemplateNodePK id, UserPK by, HistWork& await )ι->VoidTask;//each group node made from it is purged first.
		α TemplatePurging( HistTemplatePK id, UserPK by, HistWork& await )ι->VoidTask;//refused while a group uses it; else its members are purged first.
	private:
		friend struct HistResolveAwait;
		α Resolving( HistResolveAwait& await )ι->VoidTask;
		α Loading( AnyVoidAwait& await )ι->VoidTask;
		//Adds the row's group to the library, with its rows as members, and keeps its host.  Throws when its files can't be read.
		α Load( const HistGroupRow& row, const vector<HistGroupNodeRow>& nodes, const flat_map<HistTemplateNodePK,HistTemplateNodeRow>& members, str slug, SL sl )ε->sp<HistGroupHost>;
		//RemoveGroup and Close:  the host, for a caller to wait on its last flush, or null for a group that isn't live.
		α Remove( HistGroupPK id, optional<Opc::Hist::Writer> by, SL sl )ι->sp<HistGroupHost>;

		sp<Opc::Hist::Historian> _historian;//an sp read under _mutex:  a load or remove in flight keeps the one Stop let go of.
		mutable std::mutex _mutex;
		flat_map<HistGroupPK,sp<HistGroupHost>> _groups;
		bool _stopped{};
	};
}