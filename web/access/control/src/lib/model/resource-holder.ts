import { Rights } from "./permission";
import { EffectiveRight, Names, RightsSource } from "./effective-right";

export type HolderKind = "role"|"group"|"user";
type SourceRow = { permissionId:number; allowed:number; denied:number; path:{id:number; type:"group"|"role"}[] };
type HolderRow = { id:number; allowed:number; denied:number; effective?:number; sources:SourceRow[] };
//The server's resourceRights answer - Authorize::Holders, ResourceRightsAwait.cpp.
export type ResourceRightsRow = { resource:{ id:number; deleted?:string|null }; roles:HolderRow[]; groups:HolderRow[]; users:HolderRow[] };
export type NamedRow = { id:number; name?:string; slug?:string };
export type HolderLists = { roles:NamedRow[]; groups:NamedRow[]; users:NamedRow[] };

//A role, group or user one resource's grants reach, with what they come to there - a row of the resource page's tabs.  Only a
//user's rights are enforced; a role's or a group's are what it passes on.  Every source's path is relative to its row:  a
//role's runs through its member roles down to the one holding the permission (empty = held directly), a group's and a user's
//as EffectiveRight's do (empty groups = granted to the row itself).
export class ResourceHolder{
	constructor( readonly kind:HolderKind, row:HolderRow, named:NamedRow|undefined, names:Names ){
		this.id = row.id;
		this.name = named?.name ?? `#${row.id}`;
		this.slug = named?.slug;
		this.allowed = row.allowed as Rights;
		this.denied = row.denied as Rights;
		this.sources = row.sources.map( s=>({
			permissionId: s.permissionId,
			allowed: s.allowed as Rights,
			denied: s.denied as Rights,
			path: s.path.map( step=>({ ...step, name: (step.type=="group" ? names.groups : names.roles).get(step.id) }) )
		}) );
	}

	//"held directly" | "via role Viewer › Reader" for a role;  EffectiveRight's wording for a group or a user.
	describe( source:RightsSource ):string{
		if( this.kind!="role" )
			return EffectiveRight.describe( source );
		return source.path.length ? `via role ${source.path.map( s=>s.name ?? `#${s.id}` ).join(" › ")}` : "held directly";
	}
	//A mark's tooltip:  one line per source touching `right`, allows before denies - EffectiveRight.tooltip's shape.
	tooltip( right:Rights ):string{
		const lines:string[] = [];
		for( const source of this.sources ){
			if( source.allowed & right )
				lines.push( `Allowed - ${this.describe(source)}` );
		}
		for( const source of this.sources ){
			if( source.denied & right )
				lines.push( `Denied - ${this.describe(source)}` );
		}
		return lines.join( "\n" );
	}
	isDenied( right:Rights ):boolean{ return (this.denied & right)!=0; }//a deny wins, as the server's does
	isAllowed( right:Rights ):boolean{ return (this.allowed & ~this.denied & right)!=0; }

	readonly id:number;
	readonly name:string;
	readonly slug?:string;
	readonly allowed:Rights;
	readonly denied:Rights;
	readonly sources:RightsSource[];
}

//Everyone a resource's grants reach, joined to the names and slugs the server's cache does not hold.
export class ResourceHolders{
	constructor( row:ResourceRightsRow, lists:HolderLists ){
		this.enforced = !row.resource.deleted;
		const byId = ( rows:NamedRow[] )=>new Map( rows.map( r=>[r.id, r] ) );
		const roles = byId( lists.roles ), groups = byId( lists.groups ), users = byId( lists.users );
		const names:Names = { roles: ResourceHolders.names( lists.roles ), groups: ResourceHolders.names( lists.groups ) };
		const holders = ( kind:HolderKind, rows:HolderRow[], named:Map<number,NamedRow> )=>rows
			.map( r=>new ResourceHolder(kind, r, named.get(r.id), names) )
			.sort( (a,b)=>a.name.localeCompare(b.name) || a.id-b.id );
		this.roles = holders( "role", row.roles, roles );
		this.groups = holders( "group", row.groups, groups );
		this.users = holders( "user", row.users, users );
	}
	static names( rows:NamedRow[] ):Map<number,string>{ return new Map( rows.filter( r=>r.name ).map( r=>[r.id, r.name!] ) ); }

	readonly enforced:boolean;//the server's state, whatever the cached resources list says
	readonly roles:ResourceHolder[];
	readonly groups:ResourceHolder[];
	readonly users:ResourceHolder[];
}