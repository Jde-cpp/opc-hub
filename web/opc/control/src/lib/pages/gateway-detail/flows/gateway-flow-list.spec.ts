import { Component, input } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { MAT_DIALOG_DATA, MatDialog, MatDialogRef } from '@angular/material/dialog';
import { Router } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { dialogStub, snackbarStub } from 'jde-framework/testing';
import { provideRoute, routerStub, stable } from 'jde-spa/testing';
import { Flow, FlowGraph } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { GatewayHistList } from '../hist-admin/gateway-hist-list';
import { FlowCreateDialog, FlowCreated, flowSlug, flowSlugError } from './flow-create-dialog';
import { FlowDrafts } from './flow-drafts';
import { GatewayFlowList } from './gateway-flow-list';

@Component( {selector: 'gateway-hist-list', template: ''} )
class ListStub{ gateway = input<unknown>(); kind = input<string>(); url = input<string>(); }

const graph:FlowGraph = { nodes: [{id: 'a', type: 'debug', name: '', x: 0, y: 0, props: {}, wires: []}] };
const create = async ( answer:FlowCreated|undefined )=>{
	const flows = { create: vi.fn( async ( _g:unknown, flow:{name:string; slug:string} )=>new Flow( {...flow, id: 9, guid: 'g-9'} ) ) };
	const dialog = dialogStub( answer ), snackbar = snackbarStub(), router = routerStub(), gateway = { slug: 'g' };
	TestBed.configureTestingModule( {providers: [
		{ provide: FlowService, useValue: flows }, { provide: MatDialog, useValue: dialog }, { provide: SnackbarService, useValue: snackbar }, { provide: Router, useValue: router }, provideRoute()
	]} );
	TestBed.overrideComponent( GatewayFlowList, {remove: {imports: [GatewayHistList]}, add: {imports: [ListStub]}} );
	const fixture = TestBed.createComponent( GatewayFlowList );
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'url', 'apps/gateways/g' );
	await stable( fixture );
	return { fixture, list: fixture.componentInstance, flows, dialog, snackbar, router, gateway, drafts: TestBed.inject( FlowDrafts ) };
};

describe( 'GatewayFlowList', ()=>{
	it( "creates a flow and opens its page", async ()=>{
		const d = await create( {name: 'Pump', slug: 'pump', description: 'Mean rpm'} );
		await d.list.create( false );
		expect( d.dialog.open.mock.lastCall![1]!.data ).toEqual( {import: false} );
		expect( d.flows.create ).toHaveBeenCalledWith( d.gateway, {name: 'Pump', slug: 'pump', description: 'Mean rpm'} );
		expect( d.router.navigate.mock.lastCall![0] ).toEqual( ['flows', 'pump'] );
		expect( d.drafts.take( 'g-9' ) ).toBeUndefined();
	} );
	//the file's graph goes to the flow's editor as its draft, undeployed
	it( "imports a flow:  its row made, its graph the editor's draft", async ()=>{
		const d = await create( {name: 'Pump', slug: 'pump', graph} );
		await d.list.create( true );
		expect( d.dialog.open.mock.lastCall![1]!.data ).toEqual( {import: true} );
		expect( d.router.navigate.mock.lastCall![0] ).toEqual( ['flows', 'pump'] );
		expect( d.drafts.take( 'g-9' ) ).toEqual( graph );
	} );
	it( 'makes nothing where the dialog is cancelled, and says a refusal', async ()=>{
		const d = await create( undefined );
		await d.list.create( false );
		expect( d.flows.create ).not.toHaveBeenCalled();
		d.dialog.answer = {name: 'Pump', slug: 'pump'};
		d.flows.create.mockRejectedValue( new Error( "Slug 'pump' is taken." ) );
		await d.list.create( false );
		expect( d.snackbar.exception ).toHaveBeenCalledWith( "Could not create 'Pump'.", expect.anything() );
		expect( d.router.navigate ).not.toHaveBeenCalled();
		expect( d.list.busy() ).toBe( false );
	} );
} );

describe( 'FlowCreateDialog', ()=>{
	const open = ( imported:boolean )=>{
		const ref = { close: vi.fn() };
		TestBed.configureTestingModule( {providers: [{ provide: MatDialogRef, useValue: ref }, { provide: MAT_DIALOG_DATA, useValue: {import: imported} }]} );
		return { dialog: TestBed.createComponent( FlowCreateDialog ).componentInstance, ref };
	};
	it( "follows the name with the slug until the slug is typed", ()=>{
		const { dialog, ref } = open( false );
		expect( dialog.valid() ).toBe( false );
		dialog.setName( 'Pump 1: mean rpm' );
		expect( dialog.slug() ).toBe( 'pump-1-mean-rpm' );
		dialog.setSlug( 'pump1' );
		dialog.setName( 'Pump one' );
		expect( dialog.slug() ).toBe( 'pump1' );
		dialog.setSlug( '../x' );
		expect( [dialog.slugError(), dialog.valid()] ).toEqual( ["Letters, digits, '.', '_' and '-' only, starting with a letter or digit.", false] );
		dialog.setSlug( 'pump1' );
		dialog.description.set( ' Mean ' );
		dialog.apply();
		expect( ref.close ).toHaveBeenCalledWith( {name: 'Pump one', slug: 'pump1', description: 'Mean'} );
		expect( [flowSlug( '  Línea 3 / A  ' ), flowSlug( '.hidden' ), flowSlugError( 'a.b_c-1' ), flowSlugError( '-a' )] ).toEqual( ['l-nea-3-a', 'hidden', undefined, "Letters, digits, '.', '_' and '-' only, starting with a letter or digit."] );
	} );
	//the json names the flow where the form hasn't, and what the gateway would refuse of it is said, not held against the import
	it( "reads an import's json", ()=>{
		const { dialog, ref } = open( true );
		dialog.setName( 'x' );
		dialog.setSlug( 'x' );
		expect( dialog.valid() ).toBe( false );//nothing to import yet
		dialog.setText( '{' );
		expect( dialog.read().error ).toContain( 'It is not json' );
		dialog.setName( '' );
		dialog.setText( JSON.stringify( {name: 'Pump', slug: 'Bad Slug', description: 'Mean rpm', graph: {nodes: [...graph.nodes, {id: 'w', type: 'window'}]}} ) );
		expect( [dialog.name(), dialog.slug(), dialog.description()] ).toEqual( ['Pump', 'x', 'Mean rpm'] );//the slug typed stays
		expect( dialog.fault() ).toBe( 'The window node:  a window is by `duration` or by `count`, one of the two.' );
		expect( dialog.valid() ).toBe( true );
		dialog.apply();
		expect( ref.close.mock.lastCall![0] ).toMatchObject( {name: 'Pump', slug: 'x', description: 'Mean rpm', graph: {nodes: [{id: 'a'}, {id: 'w', props: {}, wires: []}]}} );
	} );
} );