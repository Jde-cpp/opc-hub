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
import { HistGroupAccess } from './hist-group-access';

const settle = async ()=>{ for( let i=0; i<3; ++i ) await new Promise( r=>setTimeout( r ) ); };
const own = { id: 9, name: 'Pumps', criteria: '5', deleted: null };
const table = { id: 2, name: 'hist_groups', criteria: null, deleted: null };
const unenforced = ( r:any )=>({...r, deleted: '2026-10-15T00:00:00Z'});
const ops = { id: 7, name: 'ops' };
//The tab over stubs:  the hub's access schema answers the resource rows and the roles, by query, and records what the tab sends;
//the dialog answers the confirmation.
const create = async ( rows:any[], roles:any[]=[ops] )=>{
	const state = { rows, roles };
	const calls:{ql:string; vars:any}[] = [];
	const mutations:string[] = [];
	const appService = {
		queryArray: vi.fn( async ( ql:string, vars:any )=>{ calls.push( {ql, vars} ); return ql.startsWith( 'resources' ) ? state.rows : state.roles; } ),
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
		expect( d.calls.map( c=>c.vars ) ).toEqual( [{schemaName: 'gateway', slug: 'histGroups'}, {schemaName: 'gateway', slug: 'histGroups', criteria: '5'}] );
		expect( d.page.own() ).toEqual( {id: 9, name: 'Pumps', enforced: true} );
		expect( d.page.table() ).toEqual( {id: 2, name: 'hist_groups', enforced: true} );
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
		expect( d.page.status().detail ).toContain( 'every edit is refused' );
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
		expect( d.calls ).toHaveLength( 4 );
		expect( d.page.status().kind ).toBe( 'own' );
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
		expect( d.calls ).toHaveLength( 2 );
	} );
	//Make own resource:  a createResource with the group's key and name, which the server leaves unenforced
	it( 'makes the own resource', async ()=>{
		const d = await create( [table] );
		await d.page.makeResource();
		expect( d.mutations ).toEqual( ['createResource( schemaName:"gateway", slug:"histGroups", criteria:"5", name:"Pumps" ){id}'] );
		expect( d.snackbar.info ).toHaveBeenCalledWith( 'Pumps has a resource of its own, not enforced yet.' );
		expect( d.calls ).toHaveLength( 4 );
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
		expect( d.calls.at( -1 )!.vars ).toEqual( {schemaName: 'gateway', slug: 'histGroups', criteria: '6'} );
		expect( d.page.own() ).toBeUndefined();//group 6 has no row of its own
		late( [own, table] );
		await first;
		expect( d.page.own() ).toBeUndefined();
	} );
} );