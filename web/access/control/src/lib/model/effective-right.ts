import { Rights } from "./permission";
import { Resource } from "./resource";

//A step on the way from an acl grant to the user:  the server sends pks and the list they belong to, the names are
//joined here from the groups/roles lists (Authorize caches neither name).
export type PathStep = { id:number; type:"group"|"role"; name?:string };
//One acl grant reaching the user, with that grant's own rights.  `path` runs top-down from the acl identity:  the groups
//from the granted one to the one holding the user, then the roles from the assigned one to the one holding the permission.
//Empty = a bare grant on the user.
export type RightsSource = { permissionId:number; allowed:Rights; denied:Rights; path:PathStep[] };
export type Names = { groups:Map<number,string>; roles:Map<number,string> };
//The server's userRights row - Authorize::UserRights, UserRightsAwait.cpp.
export type UserRightsRow = { resource:{ id:number; schemaName?:string; slug?:string; criteria?:string; deleted?:string|null }; allowed:number; denied:number; effective:number; sources:{ permissionId:number; allowed:number; denied:number; path:{id:number; type:"group"|"role"}[] }[] };

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
	//criteria-less row of the same schema+slug, its `parent` - OpcAuthorize resolves a node to the nearest configured
	//ancestor's resource, else that root, so the node row replaces its root for its subtree rather than adding to it.  One
	//without a root in the list stays top-level.
	static fromRows( rows:UserRightsRow[], resources:Resource[], names:Names ):EffectiveRight[]{
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
			const sources:RightsSource[] = row.sources.map( s=>({
				permissionId: s.permissionId,
				allowed: s.allowed as Rights,
				denied: s.denied as Rights,
				path: s.path.map( step=>({ ...step, name: (step.type=="group" ? names.groups : names.roles).get(step.id) }) )
			}) );
			return new EffectiveRight( { resource, allowed: row.allowed as Rights, denied: row.denied as Rights, effective: row.effective as Rights, sources } );
		});
		const granted = new Set( rows.map( r=>r.resource.id ) );
		//A node resource is here too (reviews/m3-closing.md #14):  granting a role on a node mints it enforced, and the OpcServer
		//resolves the node's subtree to it alone, so a user holding nothing on it is locked out there whatever they hold on the
		//table.  With the table unenforced and ungranted (a fresh install's default) the row has no parent and stays top-level.
		for( const resource of resources ){
			if( !resource.deleted && !granted.has(resource.id) )
				y.push( new EffectiveRight({ resource: resource.criteria ? new Resource( {...resource, schemaName: resource.schema, ...asTable(resource.schema, resource.slug, resource.criteria, resource)} ) : resource }) );
		}
		y.sort( (a,b)=>(a.resource.schema ?? '').localeCompare(b.resource.schema ?? '') || (a.resource.name ?? '').localeCompare(b.resource.name ?? '') || (a.resource.criteria ?? '').localeCompare(b.resource.criteria ?? '') );
		return EffectiveRight.nest( y );
	}
	static key( schema:string|undefined, slug:string|undefined ):string{ return `${schema ?? ''}/${slug ?? ''}`; }
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
		const lines:string[] = [];
		for( const source of row.sources ){
			if( source.allowed & right )
				lines.push( `Allowed - ${EffectiveRight.describe(source)}` );
		}
		for( const source of row.sources ){
			if( source.denied & right )
				lines.push( `Denied - ${EffectiveRight.describe(source)}` );
		}
		return lines.join( "\n" );
	}

	resource:Resource;
	allowed:Rights;
	denied:Rights;
	effective:Rights;
	sources:RightsSource[];
	children:EffectiveRight[] = [];//the node-scoped rows under a table row - see fromRows
	parent?:EffectiveRight;
}
