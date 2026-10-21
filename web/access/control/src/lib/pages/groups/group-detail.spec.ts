import { TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { AccessService } from '../../services/access-service';
import { GroupDetail } from './group-detail';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';

const create = ( row:any, confirm=true )=>{//confirm:  what the confirmation answers
	const mutate = vi.fn( async ( _ql:string )=>{} );
	configureDetailPage( row, {providers: [
		{ provide: AccessService, useValue: {mutate} },
		{ provide: MatDialog, useValue: dialogStub( confirm ) }
	]} );
	const page = TestBed.createComponent( GroupDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit - createComponent alone does not call it
	return { page, mutate };
};

//The group page had no Delete at all until the trio moved up into DetailPage:  a group is an identity row like a user, and
//the generic delete<Type>/restore<Type> mutations cover it - `deleteGroup(id:N)` stamps identities.deleted, verified live.
describe( 'GroupDetail delete', ()=>{
	it( 'is offered for a saved group', ()=>{
		expect( create({id: 6, name: "OPC Gateways"}).page.isNew ).toBe( false );
	} );

	it( 'is disabled for a new group', ()=>{//TestBed takes one configure per test - the two rows cannot share an `it`
		expect( create({}).page.isNew ).toBe( true );
	} );

	it( 'deletes by id', async ()=>{
		const { page, mutate } = create( {id: 6, name: "OPC Gateways"} );
		expect( page.isDeleted ).toBe( false );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "deleteGroup(id:6)" );
	} );

	it( 'restores a row the show-deleted view returned', async ()=>{
		const { page, mutate } = create( {id: 6, name: "OPC Gateways", deleted: "2026-09-09T00:00:00Z"} );
		expect( page.isDeleted ).toBe( true );
		await page.onDeleteClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "restoreGroup(id:6)" );
	} );
} );

//Purge is the hard delete, so the template offers it only on an already-deleted row and DetailPage puts a confirmation in
//front of it.  Verified against the live hub: `purgeGroup(id:N)` removes the row outright - `groups(id:N)` comes back empty.
describe( 'GroupDetail purge', ()=>{
	it( 'purges by id once confirmed', async ()=>{
		const { page, mutate } = create( {id: 6, name: "OPC Gateways", deleted: "2026-09-09T00:00:00Z"} );
		await page.onPurgeClick();
		expect( mutate ).toHaveBeenCalledExactlyOnceWith( "purgeGroup(id:6)" );
	} );

	it( 'sends nothing when the confirmation is declined', async ()=>{
		const { page, mutate } = create( {id: 6, name: "OPC Gateways", deleted: "2026-09-09T00:00:00Z"}, false );
		await page.onPurgeClick();
		expect( mutate ).not.toHaveBeenCalled();
	} );
} );