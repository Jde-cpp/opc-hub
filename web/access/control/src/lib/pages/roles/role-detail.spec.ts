import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { AccessService } from '../../services/access-service';
import { RoleDetail } from './role-detail';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';

const create = ( row:any, confirm=true )=>{//confirm:  what the confirmation answers
	const mutate = vi.fn( async ( _ql:string )=>{} );
	configureDetailPage( row, {providers: [
		{ provide: AccessService, useValue: {mutate} },
		{ provide: MatDialog, useValue: dialogStub( confirm ) }
	]} );
	const page = TestBed.createComponent( RoleDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit - createComponent alone does not call it
	return { page, mutate };
};

//Roles are their own table, not the identities view groups and users share, so unlike `deleteGroup` these three mutations
//are advertised by the schema outright: delete/restore/purgeRole are all in the hub's mutation list.
describe( 'RoleDetail delete', ()=>{
	it( 'is offered for a saved role', ()=>{
		expect( create({id: 3, name: "Admins"}).page.isNew ).toBe( false );
	} );

	it( 'is disabled for a new role', ()=>{//TestBed takes one configure per test - the two rows cannot share an `it`
		expect( create({}).page.isNew ).toBe( true );
	} );

	it( 'deletes by id', async ()=>{
		const { page, mutate } = create( {id: 3, name: "Admins"} );
		expect( page.isDeleted ).toBe( false );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "deleteRole(id:3)" );
	} );

	it( 'restores a row the show-deleted view returned', async ()=>{
		const { page, mutate } = create( {id: 3, name: "Admins", deleted: "2026-09-09T00:00:00Z"} );
		expect( page.isDeleted ).toBe( true );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "restoreRole(id:3)" );
	} );
} );

describe( 'RoleDetail purge', ()=>{
	it( 'purges by id once confirmed', async ()=>{
		const { page, mutate } = create( {id: 3, name: "Admins", deleted: "2026-09-09T00:00:00Z"} );
		await page.onPurgeClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "purgeRole(id:3)" );
	} );

	it( 'sends nothing when the confirmation is declined', async ()=>{
		const { page, mutate } = create( {id: 3, name: "Admins", deleted: "2026-09-09T00:00:00Z"}, false );
		await page.onPurgeClick();
		expect( mutate ).not.toHaveBeenCalled();
	} );
} );