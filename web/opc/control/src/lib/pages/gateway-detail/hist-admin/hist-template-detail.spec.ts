import { ComponentFixtureAutoDetect, TestBed } from '@angular/core/testing';
import { MatDialog } from '@angular/material/dialog';
import { Mutation } from 'jde-framework';
import { ExNodeId } from '../../../model/ex-node-id';
import { histThresholdBlank } from '../../../model/hist';
import { HistPick, HistTemplate } from '../../../model/hist-admin';
import { HistAdminService } from '../../../services/hist-admin-service';
import { HistTemplateDetail } from './hist-template-detail';
import { HistNodePickerDialog } from './hist-node-picker-dialog';
import { flush } from 'jde-spa/testing';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';
import { Pumps } from 'jde-opc/testing';

const row = { id: 2, name: 'Pump', slug: 'pump' };
const create = async ( pageRow:any )=>{
	const gateway = { slug: 'g', mutate: vi.fn( async ( _m:Mutation[]|string )=>{} ) };
	const mutations = ()=>gateway.mutate.mock.calls.flatMap( ([m])=>Array.isArray( m ) ? m.map( x=>x.toString()! ) : [m] );
	const admin = {
		connections: vi.fn( async ()=>[{id: 1, slug: 'local', name: 'Local'}] ),
		templateMembers: vi.fn( async ()=>[{id: 3, templateId: 2, path: [{nsu: Pumps, name: 'motorRpm'}], thresholds: {...histThresholdBlank, minTimeInterval: '1000'}}, {id: 4, templateId: 2, path: [{nsu: Pumps, name: 'status'}], thresholds: {...histThresholdBlank}}] ),
		templateGroups: vi.fn( async ()=>[{id: 5, name: 'Pumps', slug: 'pumps', connection: {id: 1, name: 'Local'}, root: '{"nsu":"urn:jde:pumps","i":5001}', deleted: false}] ),
		createTemplateMember: vi.fn( async ( _g:unknown, _id:number, path:{name:string}[] )=>{ if( path[path.length-1].name=='status' ) throw new Error( 'the same path again' ); return 9; } ),
		updateTemplateMember: vi.fn( async ()=>{} ),
		purgeTemplateMember: vi.fn( async ()=>{} )
	};
	const dialog = dialogStub( true );//a confirmation's answer;  the picker's picks are its own, none until a test gives some
	dialog.answers.set( HistNodePickerDialog, undefined );
	const {data, snackbar} = configureDetailPage( pageRow, {gateway, providers: [
		{ provide: ComponentFixtureAutoDetect, useValue: false },//the page is driven by hand:  the zoneless fixture's own first change detection would run ngOnInit a second time, onto the row #load nulled
		{ provide: HistAdminService, useValue: admin },
		{ provide: MatDialog, useValue: dialog }
	]} );
	const page = TestBed.createComponent( HistTemplateDetail ).componentInstance;
	page.ngOnInit();
	await flush( 3 );
	return { page, mutations, admin, snackbar, dialog, data };
};

const asked = ( d:{dialog:ReturnType<typeof dialogStub>} )=>(d.dialog.open.mock.lastCall![1]!.data as {message:string}).message;//what the last confirmation said

describe( 'HistTemplateDetail', ()=>{
	it( 'saves a new template by its name and slug', async ()=>{
		const d = await create( {} );
		expect( d.page.isNew ).toBe( true );
		expect( d.admin.templateMembers ).not.toHaveBeenCalled();
		d.page.properties.set( new HistTemplate( {...d.page.properties(), name: 'Pump', slug: 'pump'} ) );
		await d.page.onSubmitClick();
		expect( d.mutations() ).toEqual( ['createHistTemplate( slug:"pump", name:"Pump" )'] );
	} );
	//the members with their own columns, blank where a group node inherits;  the groups made from the template, linked under the gateway
	it( 'lists the members and the groups', async ()=>{
		const d = await create( row );
		expect( d.page.members.rows().map( m=>d.page.pathText( m ) ) ).toEqual( ['motorRpm', 'status'] );
		expect( d.page.cellText( d.page.members.rows()[0].thresholds, 'minTimeInterval' ) ).toBe( '1000' );
		expect( d.page.cellText( d.page.members.rows()[1].thresholds, 'minTimeInterval' ) ).toBe( '' );
		expect( d.page.groups() ).toHaveLength( 1 );
		expect( d.page.groupLink( d.page.groups()[0] ) ).toEqual( ['/apps/gateways', 'g', 'groups', 'pumps'] );
	} );
	//an edit of a member's own thresholds sends what changed, null for a clear, and reads the members again
	it( "edits a member's thresholds", async ()=>{
		const d = await create( row );
		const m = d.page.members.rows()[0];
		d.page.edit( m );
		expect( d.page.changed() ).toBe( false );
		d.page.fields.set( {...m.thresholds, minTimeInterval: '', maxTimeInterval: '45000'} );
		expect( d.page.changes() ).toEqual( {minTimeInterval: null, maxTimeInterval: 45000} );
		await d.page.saveEdit();
		expect( d.admin.updateTemplateMember ).toHaveBeenCalledWith( expect.anything(), 3, {minTimeInterval: null, maxTimeInterval: 45000} );
		expect( d.page.editing() ).toBeUndefined();
		expect( d.admin.templateMembers ).toHaveBeenCalledTimes( 2 );
	} );
	//Add members:  the picker browses a connection of the user's choosing under a root of theirs, and each pick's path is a member;  a
	//refusal is said and doesn't stop the rest
	it( 'adds the paths picked and says which were refused', async ()=>{
		const d = await create( row );
		d.dialog.answers.set( HistNodePickerDialog, <HistPick[]>[
			{ node: new ExNodeId( {ns: 2, i: 6012, nsu: Pumps} ), path: [{nsu: Pumps, name: 'motorRpm'}], name: 'rpm' },
			{ node: new ExNodeId( {ns: 2, i: 6011, nsu: Pumps} ), path: [{nsu: Pumps, name: 'status'}], name: 'status' }
		] );
		const open = vi.spyOn( HistNodePickerDialog, 'open' );
		await d.page.add();
		expect( open.mock.calls[0][1] ).toMatchObject( {mode: 'paths', rootEditable: true, connections: [{id: 1, slug: 'local', name: 'Local'}]} );
		expect( d.admin.createTemplateMember ).toHaveBeenCalledTimes( 2 );
		expect( d.admin.createTemplateMember.mock.calls[0].slice( 1, 3 ) ).toEqual( [2, [{nsu: Pumps, name: 'motorRpm'}]] );
		expect( d.snackbar.error ).toHaveBeenCalledWith( '1 of 2 added.  status:  the same path again' );
		open.mockRestore();
	} );
	it( 'removes a member once confirmed', async ()=>{
		const d = await create( row );
		d.dialog.answer = false;
		await d.page.remove( d.page.members.rows()[1] );
		expect( d.admin.purgeTemplateMember ).not.toHaveBeenCalled();
		d.dialog.answer = true;
		await d.page.remove( d.page.members.rows()[1] );
		expect( d.admin.purgeTemplateMember ).toHaveBeenCalledWith( expect.anything(), 4 );
		expect( asked(d) ).toContain( 'its node leaves the group made from the template.' );
	} );
	//the purge takes the node out of every group made from the template, deleted ones too, so the confirmation counts them
	it( 'counts the deleted groups a member leaves', async ()=>{
		const d = await create( row );
		const group = d.page.groups()[0];
		d.page.groups.set( [group, {...group, id: 6, deleted: true}, {...group, id: 7, deleted: true}] );
		await d.page.remove( d.page.members.rows()[1] );
		expect( asked(d) ).toContain( 'its node leaves the 3 groups made from the template, 2 of them deleted.' );
		d.page.groups.set( [{...group, deleted: true}] );
		await d.page.remove( d.page.members.rows()[1] );
		expect( asked(d) ).toContain( 'its node leaves the deleted group made from the template.' );
	} );
} );