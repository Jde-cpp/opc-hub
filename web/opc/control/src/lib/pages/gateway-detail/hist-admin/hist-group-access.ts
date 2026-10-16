import { Component, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { RouterLink } from '@angular/router';
import { AppService, confirm, errorText, Mutation, MutationType, SnackbarService } from 'jde-framework';
import { RolePermission, RolePermissionRow, roleMutation, toggleRights, toRolePermissions, withMutation } from '../../node-detail/node-access/node-rights/role-permission';
import { NodeRights } from '../../node-detail/node-access/node-rights/node-rights';

//The group as the tab needs it:  its id, the criteria of its own resource, and its name, what that resource is called
export type HistGroupRef = { id:number; name:string };
//A resource row as the tab reads it, the group's own or the table's;  `enforced` is `deleted` null, the Resources page's switch
export type HistGroupResource = { id:number; enforced:boolean };
//a resource row as resourcesQuery answers it (#14)
export type HistGroupResourceRow = { id:number; criteria?:string|null; deleted?:string|null };
//Which row governs the group (spec *Authorization*):  its own when it has one enforced, else the table's when that is, else nothing -
//reads and Resolve pass, which is configuration and gated as the tables are, and history edits are refused
export type HistGroupGovernor = 'own'|'table'|'none';
export type HistGroupAccessStatus = { kind:HistGroupGovernor; icon:string; title:string; detail:string };
//A role's permission on the table's row, as tableRolesQuery answers it:  allowed and denied are [Right] name arrays
export type HistGroupHeldRow = { id:number; permissionRight?:{ allowed:string[]; denied:string[] } };
//what a role holds through the table's row, as the Inherited column's tooltip says it
export const histGroupHeldText = ( held:{ allowed:string[]; denied:string[] } ):string=>
	[held.allowed.join( ', ' ), held.denied.length ? `denied ${held.denied.join( ', ' )}` : ''].filter( x=>x ).join( ';  ' )+", through the hist_groups table's resource";
//the hist_groups table's resource, as the gateway tests it:  Acl()->Test( "gateway", "histGroups", <group id>, … ), the schema shared by
//every gateway instance
export const histGroupResourceKey = { schemaName: 'gateway', slug: 'histGroups' };
//The name the group's own row takes:  access_resources.name and access_role_add's _resourceName are varchar(64) (access-meta.jsonnet), a
//group's name varchar(256), and MySQL's strict mode refuses a longer one outright - so its first 64 characters, counted in code points as
//the column counts them, never splitting a surrogate pair (reviews-issue/historian-group-access.md #3)
export const histGroupResourceName = ( name:string ):string=>Array.from( name ).slice( 0, 64 ).join( '' );
//the rights the gate asks for:  Read for history and a subscription, Update for createHistory, updateHistory, upsertHistory and
//resolveHistory, Delete for purgeHistory;  and Administer, which addRole and removeRole ask of the row that governs the group, so once
//its own row does, Administer there is what changes these grants - and no other page grants on that row;  None clears a row, as the
//node grid's does
export const histGroupRights = { 0: 'None', 2: 'Read', 4: 'Update', 8: 'Delete', 32: 'Administer' };

//A group's Access tab (#339):  the node page's grid of roles against rights on the group's own access_resources row, criteria its id, with
//which row governs it - the hist_groups table's row is the slug's switch, then the group's own where it has one enforced, else the
//table's (status below) - and the row's making and enforcement.  A grant saved on a group with no row of
//its own mints one, enforced, as a node's first grant does (access_role_add);  Make own resource makes one unenforced, for grants to go
//on before it takes over, and Enforce and Stop enforcing are the Resources page's switch on that row.  Everything goes through the hub's
//access schema, as the node tab's does, not the gateway.
@Component({
	selector: 'hist-group-access',
	templateUrl: './hist-group-access.html',
	styleUrls: ['./hist-admin-shared.scss', './hist-group-access.scss'],
	imports: [MatButtonModule, MatIconModule, MatProgressBarModule, MatTooltipModule, RouterLink, NodeRights]
})
export class HistGroupAccess{
	constructor(){
		effect( ()=>{//the router reuses the page from one group to the next:  read the one it holds now
			const group = this.group();
			untracked( ()=>this.load( group ) );
		} );
	}
	group = input.required<HistGroupRef>();
	readonly rights = histGroupRights;
	own = signal<HistGroupResource|undefined>( undefined );
	table = signal<HistGroupResource|undefined>( undefined );
	roles = signal<RolePermission[]>( [] );
	original:RolePermission[] = [];
	mutations = signal<Mutation[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );
	shown = signal<number|undefined>( undefined );//the group whose rows are on screen, from a read that landed;  undefined until one has
	//The rows on screen are a finished read of the group the tab is given, and nothing is on the wire:  the grid and every action wait
	//for it, so none acts on another group's rows (reviews-issue/historian-group-access.md #2), on rows a read is about to replace (#9),
	//or on a baseline a failed read left behind (#4).
	ready = computed( ()=>this.shown()==this.group().id && !this.loading() && !this.error() && !this.busy() );
	#reads = 0;//numbers each read, so only the latest lands

	static readonly resourcesQuery = 'resources( schemaName:$schemaName, slug:$slug ){ id criteria deleted }';
	static readonly rolesQuery = 'roles( deleted:null ){ id name permissionRight{ id allowed denied resource( schemaName:$schemaName, criteria:$criteria, slug:$slug ){ id criteria } } }';
	//each role's permission on the table's row, where it holds one:  what it holds on a group the table governs (#11)
	static readonly tableRolesQuery = 'roles( deleted:null ){ id permissionRight{ allowed denied resource( schemaName:$schemaName, criteria:null, slug:$slug ){ id } } }';
	//the table's rows, its own and the table's among them, and every live role with its permission on the group's own row where it has one,
	//and on the table's
	async load( group:HistGroupRef=this.group() ):Promise<void>{
		const read = ++this.#reads;
		const criteria = String( group.id );
		if( group.id!=this.shown() ){//another group:  nothing of the last one's stays on screen to act on while this one is read, or after it fails
			this.shown.set( undefined );
			this.own.set( undefined );
			this.table.set( undefined );
			this.original = [];
			this.roles.set( [] );
			this.mutations.set( [] );
		}
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const [rows, roles, tableRoles] = await Promise.all( [
				this.appService.queryArray<HistGroupResourceRow>( HistGroupAccess.resourcesQuery, histGroupResourceKey, m=>console.log( m ) ),
				this.appService.queryArray<RolePermissionRow>( HistGroupAccess.rolesQuery, {...histGroupResourceKey, criteria}, m=>console.log( m ) ),
				this.appService.queryArray<HistGroupHeldRow>( HistGroupAccess.tableRolesQuery, histGroupResourceKey, m=>console.log( m ) )
			] );
			if( read!=this.#reads )//a later read is on the wire, of this group or another, and its answer is the one to show
				return;
			const toResource = ( r:HistGroupResourceRow|undefined ):HistGroupResource|undefined=>r ? { id: Number( r.id ), enforced: r.deleted==null } : undefined;
			const own = toResource( rows.find( r=>r.criteria==criteria ) ), table = toResource( rows.find( r=>r.criteria==null ) );
			this.own.set( own );
			this.table.set( table );
			//while the table's row governs the group, what each role holds there is what it holds on the group:  the grid's Inherited column
			const inherited = new Map( table?.enforced && !own?.enforced ? tableRoles.flatMap( r=>r.permissionRight ? [[Number( r.id ), histGroupHeldText( r.permissionRight )] as const] : [] ) : [] );
			const permissions:RolePermission[] = toRolePermissions( roles, criteria ).map( r=>({...r, inherited: inherited.get( Number( r.roleId ) )}) );//a permission only where the role holds one on this group's own row
			this.original = permissions;
			this.roles.set( permissions.map( r=>({...r}) ) );
			this.mutations.set( [] );
			this.shown.set( group.id );
		}
		catch( e ){
			if( read==this.#reads )
				this.error.set( errorText( e ) ?? 'Unknown error' );
		}
		finally{
			if( read==this.#reads )
				this.loading.set( false );
		}
	}
	//Which row governs the group, as the hub's Authorize resolves it (FindActiveResourcePKLocked):  the table's row is the slug's switch,
	//consulted first - while it is unenforced no criteria row of the slug is looked at, whatever its own state - then the group's own row
	//where it has one enforced, else the table's;  and what a grant here does about it.
	status = computed<HistGroupAccessStatus>( ()=>{
		const own = this.own(), table = this.table(), name = this.group().name;
		if( !table?.enforced ){
			const next = own
				? own.enforced ? "The group's own resource is enforced already, so the grants here apply the moment the table's is." : "The group's own resource isn't enforced;  once both are, the grants here apply."
				: "A grant saved here makes the group's own resource, enforced, so the grants here apply the moment the table's is;  Make own resource makes one that isn't enforced yet.";
			return { kind: 'none', icon: 'remove_moderator', title: 'Nothing enforces it.', detail: `Anyone signed in reads ${name}'s history and may Resolve it, and every history edit - an insert, a replacement, a correction or a purge - is refused:  the hist_groups table's resource isn't enforced, and its switch, on the Resources page, is what turns checking on for the groups' own rows too.  ${next}` };
		}
		if( own?.enforced )
			return { kind: 'own', icon: 'verified_user', title: 'Governed by its own resource, enforced.', detail: `Only the roles granted here read ${name}'s history, update it or delete it, whatever they hold on the hist_groups table, and only those granted Administer here change these grants.` };
		const next = own
			? "The group's own resource isn't enforced yet:  Enforce it and the grants here take over, changed from then on only by the roles with Administer here."
			: "A grant saved here makes the group's own resource, enforced, and the grants here take over:  tick Administer for a role you hold in the same save, or they can't be changed again until it stops being enforced.  Make own resource makes one that isn't enforced yet.";
		return { kind: 'table', icon: 'shield', title: "Governed by the hist_groups table's resource, enforced.", detail: `The table's grants apply to every group without an enforced row of its own;  the Inherited column shows what each role holds there.  ${next}` };
	} );

	onToggle( event:{ role:RolePermission; rights:number } ):void{
		if( !this.ready() )
			return;
		const role = event.role;
		toggleRights( role, event.rights );
		const original = this.original.find( r=>r.roleId==role.roleId )!;
		//By key, never by id:  access_role_add finds the row by it whether or not the authorizer's cache holds the row, and makes it,
		//enforced and named for the group, where there is none.
		const mutation = roleMutation( role, original, ()=>({ ...histGroupResourceKey, criteria: role.criteria, name: histGroupResourceName( this.group().name ) }) );
		this.mutations.set( withMutation( this.mutations(), role.roleId, mutation ) );
		if( mutation )
			console.log( mutation.toString() );
	}
	async save():Promise<void>{
		const mutations = this.mutations();
		if( !mutations.length || !this.ready() )
			return;
		this.busy.set( true );
		try{
			await this.appService.mutate( mutations, m=>console.log( m ) );
		}
		catch( e ){
			this.snackbar.exception( "Could not save the group's access.", e );//the batch stays, so Save retries it
			return;
		}
		finally{
			this.busy.set( false );
		}
		this.mutations.set( [] );//applied:  were the read below to fail, Save would resend it (#4)
		await this.load();//the permission ids the server gave, and the row a first grant minted
	}
	//A resource of the group's own, made unenforced as a service's sync makes a table's (ResourceMAwait):  the table's row, or nothing,
	//governs the group until Enforce.  Needs Administer on the gateway schema's enforced roots, if any.
	async makeResource():Promise<void>{
		const group = this.group();
		if( !this.ready() || this.own() )
			return;
		this.busy.set( true );
		try{
			await this.appService.mutate( new Mutation( 'Resource', 0, {...histGroupResourceKey, criteria: String( group.id ), name: histGroupResourceName( group.name )}, MutationType.Create, ['id'] ), m=>console.log( m ) );
			this.snackbar.info( `${group.name} has a resource of its own, not enforced yet.` );
		}
		catch( e ){
			this.snackbar.exception( "Could not make the group's resource.", e );
		}
		finally{
			this.busy.set( false );
		}
		await this.load();
	}
	//the Resources page's switch on the group's own row, confirmed both ways as there:  enforcing can lock the operator out of the group,
	//and stopping opens it to whatever the table's row allows
	async setEnforced( enforce:boolean ):Promise<void>{
		const own = this.own();
		if( !own || !this.ready() )
			return;
		const name = this.group().name;
		const verb = enforce ? 'Enforce' : 'Stop enforcing';
		const pending = this.table()?.enforced ? '' : "  Nothing is checked until the hist_groups table's resource is enforced as well, on the Resources page.";
		const message = enforce
			? `Only the roles granted here will read ${name}'s history, update it or delete it, whatever they hold on the hist_groups table, and only those with Administer here will change these grants.  Grant a role you hold Read and Administer first, or you may lose your own access.${pending}`
			: `${name} goes back under the hist_groups table's resource:  its grants apply where it is enforced;  where it isn't, anyone signed in reads the history and may Resolve it, and every history edit is refused.`;
		if( !await confirm( this.dialog, {title: `${verb} ${name}?`, message, confirm: verb} ) || this.own()!==own || !this.ready() )
			return;//declined, or the group or its row changed while the dialog was open
		this.busy.set( true );
		try{
			await this.appService.mutate( `${enforce ? 'restore' : 'delete'}Resource( id:${own.id} )`, m=>console.log( m ) );
		}
		catch( e ){
			this.snackbar.exception( `${verb} failed.`, e );
		}
		finally{
			this.busy.set( false );
		}
		await this.load();
	}
	appService = inject( AppService );
	snackbar = inject( SnackbarService );
	dialog = inject( MatDialog );
}