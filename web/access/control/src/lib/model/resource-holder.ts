import { Rights } from "./permission";
import { EffectiveRight, Names, RightsSource, SourceRow } from "./effective-right";

export type HolderKind = "role"|"group"|"user";
type HolderRow = { id:number; allowed:number; denied:number; effective?:number; sources:SourceRow[] };
//The server's resourceRights answer - Authorize::Holders, ResourceRightsAwait.cpp.
export type ResourceRightsRow = { resource:{ id:number; deleted?:string|null }; roles:HolderRow[]; groups:HolderRow[]; users:HolderRow[] };
export type NamedRow = { id:number; name?:string; slug?:string };
export type HolderLists = { roles?:NamedRow[]; groups?:NamedRow[]; users?:NamedRow[] };//undefined:  a list the user may not read

//A role, group or user one resource's grants reach, with what they come to there - a row of the resource page's tabs.  Only a
//user's rights are enforced; a role's or a group's are what it passes on.  Every source's path is relative to its row:  a
//role's runs through its member roles down to the one holding the permission (empty = held directly), a group's and a user's
//as EffectiveRight's do (empty groups = granted to the row itself).
export class ResourceHolder{
	constructor( readonly kind:HolderKind, row:HolderRow, named:NamedRow|undefined, names:Names ){
		this.id = row.id;
		this.name = named?.name || `#${row.id}`;//a userCreated event can carry no name
		this.slug = named?.slug;
		this.allowed = row.allowed as Rights;
		this.denied = row.denied as Rights;
		this.effective = (row.effective ?? (row.allowed & ~row.denied)) as Rights;//a user's is the server's - what Test enforces;  only a user's is sent
		this.sources = EffectiveRight.toSources( row.sources, names );
	}

	//"held directly" | "via role Viewer › Reader" for a role;  EffectiveRight's wording for a group or a user.
	describe( source:RightsSource ):string{
		if( this.kind!="role" )
			return EffectiveRight.describe( source );
		return source.path.length ? `via role ${source.path.map( s=>s.name ?? `#${s.id}` ).join(" › ")}` : "held directly";
	}
	tooltip( right:Rights ):string{ return EffectiveRight.sourcesTooltip( this.sources, right, s=>this.describe(s) ); }
	isDenied( right:Rights ):boolean{ return (this.denied & right)!=0; }//a deny wins, as the server's does
	isAllowed( right:Rights ):boolean{ return (this.effective & right)!=0; }

	readonly id:number;
	readonly name:string;
	readonly slug?:string;
	readonly allowed:Rights;
	readonly denied:Rights;
	readonly effective:Rights;
	readonly sources:RightsSource[];
}

//Everyone a resource's grants reach, joined to the names and slugs the server's cache does not hold.
export class ResourceHolders{
	constructor( row:ResourceRightsRow, readonly lists:HolderLists ){
		this.enforced = !row.resource.deleted;
		const byId = ( rows:NamedRow[]=[] )=>new Map( rows.map( r=>[r.id, r] ) );
		const roles = byId( lists.roles ), groups = byId( lists.groups ), users = byId( lists.users );
		const names:Names = { roles: ResourceHolders.names( lists.roles ), groups: ResourceHolders.names( lists.groups ) };
		const holders = ( kind:HolderKind, rows:HolderRow[], named:Map<number,NamedRow> )=>rows
			.map( r=>new ResourceHolder(kind, r, named.get(r.id), names) )
			.sort( (a,b)=>a.name.localeCompare(b.name) || a.id-b.id );
		this.roles = holders( "role", row.roles, roles );
		this.groups = holders( "group", row.groups, groups );
		this.users = holders( "user", row.users, users );
	}
	static names( rows:NamedRow[]=[] ):Map<number,string>{ return new Map( rows.filter( r=>r.name ).map( r=>[r.id, r.name!] ) ); }
	//Whether `lists` holds every row - a path's roles and groups are rows too.  A list the user may not read holds none, and
	//reading it again would not change that.
	static covers( row:ResourceRightsRow, lists:HolderLists ):boolean{
		const has = ( rows:HolderRow[], named?:NamedRow[] )=>{
			const ids = named && new Set( named.map( n=>n.id ) );
			return !ids || rows.every( r=>ids.has(r.id) );
		};
		return has( row.roles, lists.roles ) && has( row.groups, lists.groups ) && has( row.users, lists.users );
	}

	readonly enforced:boolean;//the server's state, whatever the cached resources list says
	readonly roles:ResourceHolder[];
	readonly groups:ResourceHolder[];
	readonly users:ResourceHolder[];
}