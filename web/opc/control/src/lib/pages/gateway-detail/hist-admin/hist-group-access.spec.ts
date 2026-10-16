if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { provideRouter } from '@angular/router';
import { vi } from 'vitest';
import { Rights } from 'jde-access';
import { AppService, SnackbarService } from 'jde-framework';
import { HistGroupAccess, histGroupResourceName } from './hist-group-access';

const settle = async ()=>{ for( let i=0; i<3; ++i ) await new Promise( r=>setTimeout( r ) ); };
const own = { id: 9, criteria: '5', deleted: null };//as resourcesQuery answers:  no name, which the tab never shows (#13)
const table = { id: 2, criteria: null, deleted: null };
const unenforced = ( r:any )=>({...r, deleted: '2026-10-15T00:00:00Z'});
const ops = { id: 7, name: 'ops' };
//The tab over stubs:  the hub's access schema answers the resource rows and the roles, by query, and records what the tab sends;
//the dialog answers the confirmation.
const create = async ( rows:any[], roles:any[]=[ops], tableRoles:any[]=[] )=>{
	const state = { rows, roles, tableRoles };
	const calls:{ql:string; vars:any}[] = [];
	const mutations:string[] = [];
	const appService = {
		queryArray: vi.fn( async ( ql:string, vars:any )=>{ calls.push( {ql, vars} ); return ql.startsWith( 'resources' ) ? state.rows : ql.includes( 'criteria:null' ) ? state.tableRoles : state.roles; } ),
		mutate: vi.fn( async ( m:any )=>{ mutations.push( ...(Array.isArray( m ) ? m.map( x=>x.toString() ) : [typeof m=='string' ? m : m.toString()]) ); } )
	};
	const snackbar = { exception: vi.fn(), error: vi.fn(), info: vi.fn() };
	const dialog = { confirmed: true, open: ()=>({ afterClosed: ()=>({ subscribe: ( f:( y:unknown )=>void )=>f( dialog.confirmed ) }) }) };
	TestBed.configureTestingModule({ providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },
		provideRouter( [] ),//the Resources link
		{ provide: AppService, useValue: appService },
		{ provide: SnackbarService, useValue: snackbar },
		{ provide: MatDialog, useValue: dialog }
	]});
	const fixture = TestBed.createComponent( HistGroupAccess );
	fixture.componentRef.setInput( 'group', {id: 5, name: 'Pumps'} );
	fixture.detectChanges();//runs the effect that loads
	await settle();
	return { page: fixture.componentInstance, fixture, appService, calls, mutations, snackbar, dialog, state };
};

describe( 'HistGroupAccess', ()=>{
	//the rows are asked under the gateway schema's histGroups slug, the roles with the group's id as the criteria, and an enforced row of
	//the group's own governs it under the table's
	it( "reads the group's own row and the table's, and says which governs", async ()=>{
		const d = await create( [own, table], [{...ops, permissionRight: {id: 3, allowed: ['Read', 'Update'], denied: [], resource: {id: 9, criteria: '5'}}}] );
		expect( d.calls.map( c=>c.vars ) ).toEqual( [{schemaName: 'gateway', slug: 'histGroups'}, {schemaName: 'gateway', slug: 'histGroups', criteria: '5'}, {schemaName: 'gateway', slug: 'histGroups'}] );
		expect( d.calls[0].ql ).not.toContain( 'name' );
		expect( d.page.own() ).toEqual( {id: 9, enforced: true} );
		expect( d.page.table() ).toEqual( {id: 2, enforced: true} );
		expect( d.page.status().kind ).toBe( 'own' );
		expect( d.page.roles() ).toEqual( [{roleId: 7, roleName: 'ops', permissionId: 3, allowed: Rights.Read|Rights.Update, denied: Rights.None, resourceId: 9, criteria: '5'}] );
	} );
	//an own row that isn't enforced leaves the table's in charge where that is;  the table's row unenforced is nothing enforced - open reads
	//and refused edits - whatever the group's own row says, since the hub consults a slug's criteria rows only while its root is active
	it( "falls back to the table's row, then to nothing", async ()=>{
		const d = await create( [unenforced( own ), table] );
		expect( d.page.status().kind ).toBe( 'table' );
		expect( d.page.status().detail ).toContain( "own resource isn't enforced" );
		d.state.rows = [unenforced( table )];
		await d.page.load();
		expect( d.page.own() ).toBeUndefined();
		expect( d.page.status().kind ).toBe( 'none' );
		expect( d.page.status().detail ).toContain( 'may Resolve it, and every history edit' );//#7:  resolveHistory passes where nothing is enforced
		expect( d.page.status().detail ).toContain( 'Make own resource' );
		d.state.rows = [own, unenforced( table )];
		await d.page.load();
		expect( d.page.own()?.enforced ).toBe( true );
		expect( d.page.status().kind ).toBe( 'none' );
		expect( d.page.status().detail ).toContain( 'enforced already' );
	} );
	//a grant on a group with no row of its own names the row by its key, so access_role_add makes it, enforced and named for the group;
	//Save sends the batch and reads again, since the server's permission ids and the new row are what the next toggle diffs against
	it( 'saves a grant by the key of the row it mints, and reads again', async ()=>{
		const d = await create( [table] );
		const role = d.page.roles()[0];
		d.page.onToggle( {role, rights: Rights.Read} );
		d.page.onToggle( {role, rights: Rights.Delete} );
		expect( d.page.mutations() ).toHaveLength( 1 );
		d.state.rows = [own, table];
		await d.page.save();
		expect( d.mutations ).toEqual( ['addRole( id:7, permissionRight:{allowed:10, denied:0, resource:{schemaName:"gateway", slug:"histGroups", criteria:"5", name:"Pumps"}} )'] );
		expect( d.page.mutations() ).toEqual( [] );
		expect( d.calls ).toHaveLength( 6 );
		expect( d.page.status().kind ).toBe( 'own' );
	} );
	//Administer is on the grid:  once the group's own row governs, addRole and removeRole on it ask Administer of that row, and no other page
	//grants on it (the Resources page lists no criteria row) - reviews-issue/historian-group-access.md #1
	it( 'grants Administer, and says changing the grants needs it', async ()=>{
		const d = await create( [table] );
		expect( d.fixture.nativeElement.querySelector( 'mat-header-cell.mat-column-Administer' ) ).not.toBeNull();
		expect( d.page.status().detail ).toContain( 'tick Administer for a role you hold' );
		d.page.onToggle( {role: d.page.roles()[0], rights: Rights.Administer} );
		expect( d.page.mutations()[0].toString() ).toBe( 'addRole( id:7, permissionRight:{allowed:32, denied:0, resource:{schemaName:"gateway", slug:"histGroups", criteria:"5", name:"Pumps"}} )' );
		d.state.rows = [own, table];
		await d.page.load();
		expect( d.page.status().detail ).toContain( 'only those granted Administer here change these grants' );
	} );
	//#3:  the row is named for the group, whose name runs to 256 characters, and access_resources.name to 64 - MySQL refused the first grant
	//and Make own resource outright.  Cut in code points, as the column counts:  a slice of UTF-16 units would split the emoji below.
	it( "names the group's row with its first 64 characters", async ()=>{
		const cut = 'é'.repeat( 63 )+'😀';
		expect( histGroupResourceName( cut+'xyz' ) ).toBe( cut );
		expect( histGroupResourceName( 'Pumps' ) ).toBe( 'Pumps' );
		const d = await create( [table] );
		d.fixture.componentRef.setInput( 'group', {id: 5, name: cut+' and the rest of a long name'} );
		d.fixture.detectChanges();
		await settle();
		d.page.onToggle( {role: d.page.roles()[0], rights: Rights.Read} );
		expect( d.page.mutations()[0].toString() ).toContain( `criteria:"5", name:"${cut}"}` );
		await d.page.makeResource();
		expect( d.mutations ).toEqual( [`createResource( schemaName:"gateway", slug:"histGroups", criteria:"5", name:"${cut}" ){id}`] );
	} );
	//#11:  while the table's row governs the group, what a role holds there is what it holds on the group - the Inherited column, the rights
	//its tooltip;  once the group's own row governs, or nothing does, the table's grants are none of the group's
	it( "shows what each role holds through the table's row while that governs", async ()=>{
		const viewer = { id: 8, name: 'viewer' };
		const held = [{id: 7, permissionRight: {allowed: ['Read', 'Update'], denied: ['Delete'], resource: {id: 2}}}, {id: 8}];
		const d = await create( [table], [ops, viewer], held );
		expect( d.page.status().kind ).toBe( 'table' );
		expect( d.page.status().detail ).toContain( 'the Inherited column shows what each role holds there' );
		expect( d.page.roles().map( r=>r.inherited ) ).toEqual( ["Read, Update;  denied Delete, through the hist_groups table's resource", undefined] );
		d.state.rows = [own, table];
		await d.page.load();
		expect( d.page.status().kind ).toBe( 'own' );
		expect( d.page.roles().map( r=>r.inherited ) ).toEqual( [undefined, undefined] );
		d.state.rows = [unenforced( table )];
		await d.page.load();
		expect( d.page.status().kind ).toBe( 'none' );
		expect( d.page.roles().map( r=>r.inherited ) ).toEqual( [undefined, undefined] );
	} );
	//a role's permission cleared is a Remove of it;  a failed save keeps the batch for a retry
	it( 'removes a cleared permission, and keeps the batch when the save fails', async ()=>{
		const d = await create( [own, table], [{...ops, permissionRight: {id: 3, allowed: ['Read'], denied: [], resource: {id: 9, criteria: '5'}}}] );
		const role = d.page.roles()[0];
		d.page.onToggle( {role, rights: 0} );
		expect( d.page.mutations()[0].toString() ).toBe( 'removeRole( id:7, permissionRight:{id:3} )' );
		d.appService.mutate.mockRejectedValueOnce( new Error( 'Forbidden' ) );
		await d.page.save();
		expect( d.snackbar.exception ).toHaveBeenCalled();
		expect( d.page.mutations() ).toHaveLength( 1 );
		expect( d.calls ).toHaveLength( 3 );
	} );
	//Make own resource:  a createResource with the group's key and name, which the server leaves unenforced
	it( 'makes the own resource', async ()=>{
		const d = await create( [table] );
		await d.page.makeResource();
		expect( d.mutations ).toEqual( ['createResource( schemaName:"gateway", slug:"histGroups", criteria:"5", name:"Pumps" ){id}'] );
		expect( d.snackbar.info ).toHaveBeenCalledWith( 'Pumps has a resource of its own, not enforced yet.' );
		expect( d.calls ).toHaveLength( 6 );
	} );
	//Enforce and Stop enforcing are restore and delete on the own row, each once confirmed
	it( 'enforces and stops enforcing once confirmed, and not otherwise', async ()=>{
		const d = await create( [unenforced( own ), table] );
		d.dialog.confirmed = false;
		await d.page.setEnforced( true );
		expect( d.mutations ).toEqual( [] );
		d.dialog.confirmed = true;
		await d.page.setEnforced( true );
		expect( d.mutations ).toEqual( ['restoreResource( id:9 )'] );
		d.state.rows = [own, table];
		await d.page.load();
		await d.page.setEnforced( false );
		expect( d.mutations ).toEqual( ['restoreResource( id:9 )', 'deleteResource( id:9 )'] );
	} );
	//the router reuses the page for another group:  the rows read for the first are dropped once the second is the input
	it( 'reads the group it is given now', async ()=>{
		const d = await create( [own, table] );
		let late!:( rows:any[] )=>void;
		d.appService.queryArray.mockImplementationOnce( ()=>new Promise<any[]>( r=>late = r ) );
		const first = d.page.load();
		d.fixture.componentRef.setInput( 'group', {id: 6, name: 'Fans'} );
		d.fixture.detectChanges();
		await settle();
		expect( d.calls.filter( c=>c.vars.criteria ).at( -1 )!.vars ).toEqual( {schemaName: 'gateway', slug: 'histGroups', criteria: '6'} );
		expect( d.page.own() ).toBeUndefined();//group 6 has no row of its own
		late( [own, table] );
		await first;
		expect( d.page.own() ).toBeUndefined();
	} );
	//reviews-issue/historian-group-access.md #2:  group 5's rows, its row's id among them, used to stay until group 6's read landed, and for
	//good when it failed - Stop enforcing un-enforced group 5, and a tick granted on '5' under group 6's name
	it( "clears the last group's rows, and acts on nothing until the next group's are read", async ()=>{
		const d = await create( [own, table], [{...ops, permissionRight: {id: 3, allowed: ['Read'], denied: [], resource: {id: 9, criteria: '5'}}}] );
		d.page.onToggle( {role: d.page.roles()[0], rights: Rights.Update} );
		expect( d.page.mutations() ).toHaveLength( 1 );
		d.appService.queryArray.mockRejectedValue( new Error( 'Forbidden' ) );
		d.fixture.componentRef.setInput( 'group', {id: 6, name: 'Fans'} );
		d.fixture.detectChanges();
		expect( d.fixture.nativeElement.querySelector( 'section.governs' ) ).toBeNull();
		await settle();
		expect( d.page.error() ).toBeDefined();
		expect( d.page.own() ).toBeUndefined();
		expect( d.page.roles() ).toEqual( [] );
		expect( d.page.mutations() ).toEqual( [] );
		expect( d.page.ready() ).toBe( false );
		await d.page.setEnforced( false );
		await d.page.makeResource();
		await d.page.save();
		expect( d.mutations ).toEqual( [] );
	} );
	//#4:  a save that landed, then a read that failed, left the applied batch in Save, which resent removes already gone
	it( 'clears an applied batch when the read after it fails, and waits for a good read', async ()=>{
		const d = await create( [own, table], [{...ops, permissionRight: {id: 3, allowed: ['Read'], denied: [], resource: {id: 9, criteria: '5'}}}] );
		d.page.onToggle( {role: d.page.roles()[0], rights: 0} );
		d.appService.queryArray.mockRejectedValueOnce( new Error( 'offline' ) );
		await d.page.save();
		expect( d.mutations ).toEqual( ['removeRole( id:7, permissionRight:{id:3} )'] );
		expect( d.page.mutations() ).toEqual( [] );
		expect( d.page.error() ).toBeDefined();
		expect( d.page.ready() ).toBe( false );
		d.state.roles = [ops];
		await d.page.load();
		expect( d.page.ready() ).toBe( true );
		expect( d.page.roles()[0].permissionId ).toBe( 0 );
	} );
	//#9:  of two reads of one group, whichever answered last landed, the earlier among them;  and a tick or an action meanwhile went on
	//rows about to be replaced
	it( 'lands only the latest read, and offers nothing while one is on the wire', async ()=>{
		const d = await create( [table] );
		let early!:( rows:any[] )=>void;
		d.appService.queryArray.mockImplementationOnce( ()=>new Promise<any[]>( r=>early = r ) );
		const first = d.page.load();
		expect( d.page.ready() ).toBe( false );
		d.page.onToggle( {role: d.page.roles()[0], rights: Rights.Read} );
		expect( d.page.mutations() ).toEqual( [] );
		await d.page.makeResource();
		expect( d.mutations ).toEqual( [] );
		d.state.rows = [unenforced( own ), table];
		await d.page.load();
		expect( d.page.own()?.id ).toBe( 9 );
		early( [table] );
		await first;
		expect( d.page.own()?.id ).toBe( 9 );
		expect( d.page.ready() ).toBe( true );
	} );
} );