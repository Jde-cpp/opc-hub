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
	//A template member as resolveHistory answers it (spec *Templates*):  its row and path, the node it resolved to, and
	//Good once it has a hist_group_nodes row, else why it hasn't.
	struct HistResolved final{ HistTemplateNodePK TemplateNode; string Path; optional<ExNodeId> Node; StatusCode Status; };
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
	//monitored, as the host's reads and /hist then answer with an error (5c).
	struct HistHost final : noncopyable{
		Ω Instance()ι->HistHost&;
		//The settings, the library, the groups the tables hold and their items.  Blocks on the load:  Gateway::Connect's thread.
		α Start()ε->void;
		//Every item goes and every group's last flush runs:  before the UA clients go, while the executor still runs.
		α Stop()ι->void;
		α Enabled()Ι->bool{ return _historian && _historian->Enabled(); }
		α Historian()Ι->Opc::Hist::Historian*{ return _historian.get(); }
		α Find( HistGroupPK id )Ι->sp<HistGroupHost>;
		α Groups()Ι->vector<sp<HistGroupHost>>;
		//The collector's credential for a connection (spec *Collector contract*):  /gateway/hist/credentials/<slug>'s user and
		//password, else the process's own certificate.
		α Collector( const ServerCnnctnNK& slug )Ι->Credential;
		α Writer( UserPK by )Ι->Opc::Hist::Writer;
		//Resolves every member of the group's template that has no hist_group_nodes row yet:  each path that resolves on the
		//group's connection, from its root, becomes a row through createHistGroupNode as `by`, whose hook joins the node.  One
		//answer per member of the template.  A server that can't be reached fails every pending member with the connect's
		//status, and a member whose node another group already holds answers Bad_NodeIdExists.  A group with no template has
		//no members to answer.  Throws for a group that doesn't exist.
		α Resolve( HistGroupPK group, UserPK by, SRCE )ι->HistResolveAwait{ return { group, by, sl }; }

		//HistGroupMAwait's:  the live side of each table edit, after the row is written, or before it goes.
		α GroupCreated( HistGroupPK id, UserPK by, HistWork& await )ι->VoidTask;
		α GroupEdited( DB::Key key, HistWork& await )ι->VoidTask;//its threshold columns:  every member's re-resolved.
		α GroupDeleted( DB::Key key, UserPK by, HistWork& await )ι->VoidTask;//every member leaves, by the caller.
		α GroupRestored( DB::Key key, UserPK by, HistWork& await )ι->VoidTask;//its rows join again.
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

		up<Opc::Hist::Historian> _historian;
		mutable std::mutex _mutex;
		flat_map<HistGroupPK,sp<HistGroupHost>> _groups;
		bool _stopped{};
	};
}