import { Rights } from "./permission";
import { Resource } from "./resource";

//A step on the way from an acl grant to the user:  the server sends pks and the list they belong to, the names are
//joined here from the groups/roles lists (Authorize caches neither name).
export type PathStep = { id:number; type:"group"|"role"; name?:string };
//One acl grant reaching the user, with that grant's own rights.  `path` runs top-down from the acl identity:  the groups
//from the granted one to the one holding the user, then the roles from the assigned one to the one holding the permission.
//Empty = a bare grant on the user.  `onTable`:  a node row's copy of a grant on its table, which holds under the node too.
export type RightsSource = { permissionId:number; allowed:Rights; denied:Rights; path:PathStep[]; onTable?:boolean };
export type Names = { groups:Map<number,string>; roles:Map<number,string> };
//The lists the names come from, kept by the tab across its records (reviews-issue/252-effective-rights-group.md #12) - undefined:
//a list the user may not read.
export type PathLists = { groups?:{id:number; name?:string}[]; roles?:{id:number; name?:string}[] };
//A tab's answer:  its rows, and the lists that named them, for the next record.
export type RightsAnswer = { rights:EffectiveRight[]; lists:PathLists };
//A RightsSource as the server sends it - Server::SourcesJson, RightsSourceJson.h.
export type SourceRow = { permissionId:number; allowed:number; denied:number; path:{id:number; type:"group"|"role"}[] };
//The server's userRights row, and groupRights' - Authorize::UserRights/GroupRights, RightsJson in IdentityRightsAwait.cpp.
export type UserRightsRow = { resource:{ id:number; schemaName?:string; slug?:string; criteria?:string; deleted?:string|null }; allowed:number; denied:number; effective:number; sources:SourceRow[] };

//What a user ends up with on one resource through every path - the read-only twin of Permission for the Effective rights tab.
export class EffectiveRight{
	constructor( obj:Partial<EffectiveRight> & {resource:Resource} ){
		this.resource = obj.resource;
		this.allowed = obj.allowed ?? Rights.None;
		this.denied = obj.denied ?? Rights.None;
		this.effective = obj.effective ?? (this.allowed & ~this.denied);
		this.sources = obj.sources ?? [];
	}

	//The server's rows joined to what it does not cache - resource name and available rights from the resources list, path
	//names from the two lists - plus a row per ENFORCED resource the user has no grant on:  an enforced resource with nothing
	//is a lockout, which is what the first-run admin is looking for, and a silent absence would hide it.  Unenforced
	//resources without a grant are left out - they are open to everyone, there is nothing to say.
	//The result is nested:  a criteria row (a node-scoped resource, minted when a role is granted on a node) sits under the
	//criteria-less row of the same schema+slug, its `parent`.  OpcAuthorize adds a node's grants to its table's, a deny on
	//either winning (reviews-issue/252-effective-rights-group.md #1), so a granted node row carries its table's grants too, and
	//a node is a lockout only where the table gives the user nothing.  A node configured inside another also gets that one's
	//grants on the server, which this list - folding every node under its table - cannot show.  One without a root in the
	//list stays top-level.
	//`lockouts` false leaves the lockout rows out - a group's tab:  what a group does not give, its members may hold elsewhere.
	static fromRows( rows:UserRightsRow[], resources:Resource[], names:Names, lockouts=true ):EffectiveRight[]{
		const byId = new Map( resources.map( r=>[r.id, r] ) );
		const roots = new Map( resources.filter( r=>!r.criteria ).map( r=>[EffectiveRight.key(r.schema, r.slug), r] ) );
		//A node resource shows as its table:  its own name is the slug access_role_add coalesced ("nodeIds" under the synced
		//"node_ids"), and it carries no `allowed` - it offers what its table offers, or no lockout mark would render.
		const asTable = ( schema:string|undefined, slug:string|undefined, criteria:string|undefined, cached:Resource|undefined )=>{
			const root = roots.get( EffectiveRight.key(schema, slug) );
			return criteria
				? { name: root?.name ?? cached?.name ?? slug, availableRights: root?.availableRights ?? Rights.All }
				: { name: cached?.name ?? root?.name ?? slug, availableRights: cached?.availableRights ?? Rights.All };
		};
		const y = rows.map( row=>{
			const cached = byId.get( row.resource.id );
			//the row's own schema/slug/criteria/deleted win:  loadResources() filters criteria:null, so a node resource is only ever known
			//from here - and takes its root's name, since its own is the QL slug access_role_add coalesced ("nodeIds" under "node_ids")
			const schema = row.resource.schemaName ?? cached?.schema, slug = row.resource.slug ?? cached?.slug, criteria = row.resource.criteria || undefined;
			const resource = new Resource( { ...(cached ?? {}), id: row.resource.id, schemaName: schema, slug, criteria, deleted: row.resource.deleted ?? undefined, ...asTable(schema, slug, criteria, cached) } );
			return new EffectiveRight( { resource, allowed: row.allowed as Rights, denied: row.denied as Rights, effective: row.effective as Rights, sources: EffectiveRight.toSources(row.sources, names) } );
		});
		const granted = new Set( rows.map( r=>r.resource.id ) );
		const onTables = new Map( y.filter( r=>!r.resource.criteria ).map( r=>[EffectiveRight.key(r.resource.schema, r.resource.slug), r.effective] ) );
		//A node resource is here too (reviews/m3-closing.md #14):  granting a role on a node mints it enforced, so a user holding
		//nothing on it or on its table is locked out of its subtree.  What the table gives holds there, enforced or not.  With
		//the table unenforced and ungranted (a fresh install's default) the row has no parent and stays top-level.
		for( const resource of lockouts ? resources : [] ){
			if( resource.criteria && onTables.get(EffectiveRight.key(resource.schema, resource.slug)) )
				continue;
			if( !resource.deleted && !granted.has(resource.id) )
				y.push( new EffectiveRight({ resource: resource.criteria ? new Resource( {...resource, schemaName: resource.schema, ...asTable(resource.schema, resource.slug, resource.criteria, resource)} ) : resource }) );
		}
		y.sort( (a,b)=>(a.resource.schema ?? '').localeCompare(b.resource.schema ?? '') || (a.resource.name ?? '').localeCompare(b.resource.name ?? '') || (a.resource.criteria ?? '').localeCompare(b.resource.criteria ?? '') );
		const nested = EffectiveRight.nest( y );
		for( const root of nested )
			root.children.filter( child=>child.sources.length ).forEach( child=>child.inherit(root) );
		return nested;
	}
	static key( schema:string|undefined, slug:string|undefined ):string{ return `${schema ?? ''}/${slug ?? ''}`; }
	//Whether `lists` names every group and role the rows' paths step through.  A list the user may not read names none, and
	//reading it again would not change that.
	static covers( rows:UserRightsRow[], lists:PathLists ):boolean{
		const ids = ( named?:{id:number}[] )=>named && new Set( named.map( n=>n.id ) );
		const groups = ids( lists.groups ), roles = ids( lists.roles );
		return rows.every( r=>r.sources.every( s=>s.path.every( step=>{ const known = step.type=="group" ? groups : roles; return !known || known.has( step.id ); } ) ) );
	}
	//The server's sources with each path step named from the lists - the resource page's holders' too.
	static toSources( rows:SourceRow[], names:Names ):RightsSource[]{
		return rows.map( s=>({
			permissionId: s.permissionId,
			allowed: s.allowed as Rights,
			denied: s.denied as Rights,
			path: s.path.map( step=>({ ...step, name: (step.type=="group" ? names.groups : names.roles).get(step.id) }) )
		}) );
	}
	//criteria rows under the criteria-less row of the same schema+slug; the order of `rows` is kept, so children stay sorted
	static nest( rows:EffectiveRight[] ):EffectiveRight[]{
		const roots = new Map( rows.filter( r=>!r.resource.criteria ).map( r=>[EffectiveRight.key(r.resource.schema, r.resource.slug), r] ) );
		const y:EffectiveRight[] = [];
		for( const row of rows ){
			const root = row.resource.criteria ? roots.get( EffectiveRight.key(row.resource.schema, row.resource.slug) ) : undefined;
			if( root ){
				row.parent = root;
				root.children.push( row );
			}
			else
				y.push( row );
		}
		return y;
	}

	//The table's grants added to a granted node row's, as OpcAuthorize adds them, a deny on either winning.
	inherit( table:EffectiveRight ):void{
		this.allowed |= table.allowed;
		this.denied |= table.denied;
		this.effective = this.allowed & ~this.denied;
		this.sources = [...this.sources, ...table.sources.map( s=>({ ...s, onTable: true }) )];
	}

	get enforced():boolean{ return !this.resource.deleted; }
	get isLockout():boolean{ return this.enforced && this.sources.length==0; }

	//"direct grant" | "role Viewer" | "role Owner › Viewer via group Ops › Ops-EU" - the path read top-down, pks where a name is missing.
	static describe( source:RightsSource ):string{
		const label = ( step:PathStep )=>step.name ?? `#${step.id}`;
		const groups = source.path.filter( s=>s.type=="group" ).map( label );
		const roles = source.path.filter( s=>s.type=="role" ).map( label );
		if( !groups.length && !roles.length )
			return "direct grant";
		const parts:string[] = [];
		if( roles.length )
			parts.push( `role ${roles.join(" › ")}` );
		if( groups.length )
			parts.push( `${roles.length ? "via " : ""}group ${groups.join(" › ")}` );
		return parts.join( " " );
	}

	//A mark's tooltip:  one line per source that touches `right`, allows before denies - or the lockout explanation for a row
	//with no source.  The marks are the only place provenance lives:  a row-level summary on the Resource cell repeated them,
	//and sat over the cell's expand button and node link.
	static tooltip( row:EffectiveRight, right:Rights ):string{
		if( row.isLockout )
			return "No grant on an enforced resource - every action is denied.";
		return EffectiveRight.sourcesTooltip( row.sources, right, s=>s.onTable ? `${EffectiveRight.describe(s)}, on the table` : EffectiveRight.describe(s) );
	}
	//One line per source that touches `right`, allows before denies - `describe` words each, as the resource page's role rows
	//do their own way.
	static sourcesTooltip( sources:RightsSource[], right:Rights, describe:( source:RightsSource )=>string ):string{
		const lines = ( bits:"allowed"|"denied", label:string )=>sources.filter( s=>s[bits] & right ).map( s=>`${label} - ${describe(s)}` );
		return [...lines( "allowed", "Allowed" ), ...lines( "denied", "Denied" )].join( "\n" );
	}

	resource:Resource;
	allowed:Rights;
	denied:Rights;
	effective:Rights;
	sources:RightsSource[];
	children:EffectiveRight[] = [];//the node-scoped rows under a table row - see fromRows
	parent?:EffectiveRight;
}