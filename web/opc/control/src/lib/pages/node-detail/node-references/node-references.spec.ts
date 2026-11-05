import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { NodeId } from '../../../model/node-id';
import { Reference } from '../../../model/reference';
import { NodeReferences } from './node-references';
import { flush } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
import { fakeGateway, variable } from 'jde-opc/testing';

//the gateway's `references` rows for a pump, as NodeQLAwait answers them:  the type named from the cache, the target with its
//class and type definition
const rows = [
	{ direction: 'Forward', refType: {i: 47, name: 'HasComponent'}, target: {ns: 2, i: 6011, name: 'status', browse: {ns: 2, name: 'status'}}, class: 2, typeDef: {i: 63, name: 'BaseDataVariableType'} },
	{ direction: 'Forward', refType: {i: 40, name: 'HasTypeDefinition'}, target: {ns: 2, i: 1001, name: 'PumpType', browse: {ns: 2, name: 'PumpType'}}, class: 8, typeDef: null },
	{ direction: 'Inverse', refType: {i: 35}, target: {i: 85, name: 'Objects', browse: {ns: 0, name: 'Objects'}}, class: 1, typeDef: {i: 61, name: 'FolderType'} },
	{ direction: 'Forward', refType: {i: 47, name: 'HasComponent'}, target: {ns: 2, i: 6055, name: 'toHertz', browse: {ns: 2, name: 'toHertz'}}, class: 4, typeDef: null }
];

describe( 'NodeReferences', ()=>{
	//the tab over a gateway that answers the references and places a target - `paths` by its id's numeric part - or fails to
	const create = ( paths:Record<number, string|null> = {}, failing = false )=>{
		const gateway = Object.assign( fakeGateway( 'gw' ), {
			references: vi.fn( async ()=>{ if( failing ) throw new Error( "no session" ); return rows.map( r=>new Reference( r ) ); } ),
			pagePath: vi.fn( async ( _opc:string, id:NodeId )=>paths[<number>id.id] ?? null )
		} );
		const snackbar = snackbarStub(), navigate = vi.fn();
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: {navigate} },
			{ provide: SnackbarService, useValue: snackbar }
		]});
		const fixture = TestBed.createComponent( NodeReferences );
		fixture.componentRef.setInput( 'pageData', {gateway, server: {connection: {slug: 'cn'}}} );
		fixture.componentRef.setInput( 'node', variable( 5001, 'pump1' ) );
		fixture.detectChanges();
		return { fixture, page: fixture.componentInstance, gateway, snackbar, navigate };
	};

	it( 'lists every reference with its direction, type, target, class and type definition', async ()=>{
		const { fixture, page, gateway } = create();
		await flush( 2 );
		fixture.detectChanges();
		expect( gateway.references ).toHaveBeenCalledWith( 'cn', expect.objectContaining( {ns: 2, id: 5001} ) );
		expect( page.rows().map( r=>[r.direction, r.refType.label, r.targetLabel, r.className, r.typeDef?.label] ) ).toEqual( [
			['Forward', 'HasComponent', 'status', 'Variable', 'BaseDataVariableType'],
			['Forward', 'HasTypeDefinition', 'PumpType', 'ObjectType', undefined],
			['Inverse', 'i=35', 'Objects', 'Object', 'FolderType'],//a type the cache had no name for shows its id
			['Forward', 'HasComponent', 'toHertz', 'Method', undefined]
		] );
		const cells = [...fixture.nativeElement.querySelectorAll( 'mat-row' )].map( r=>[...r.querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() ) );
		expect( cells[0] ).toEqual( ['Forward', 'HasComponent', 'status', 'Variable', 'BaseDataVariableType'] );
	} );

	it( 'opens a target by the path the gateway walks up, a method by its object, and says so for one it cannot place', async ()=>{
		const { page, navigate, snackbar } = create( {6011: 'Objects/2~pump1/2~status', 6055: 'Objects/2~pump1/2~toHertz'} );
		await flush( 2 );
		await page.open( page.rows()[0] );
		expect( navigate ).toHaveBeenLastCalledWith( ['/gateways', 'gw', 'cn', 'Objects', '2~pump1', '2~status'] );
		await page.open( page.rows()[3] );
		expect( navigate ).toHaveBeenLastCalledWith( ['/gateways', 'gw', 'cn', 'Objects', '2~pump1'] );
		await page.open( page.rows()[1] );
		expect( navigate ).toHaveBeenCalledTimes( 2 );
		expect( snackbar.info ).toHaveBeenCalledWith( expect.stringContaining( 'PumpType has no page' ) );
	} );

	it( 'shows the error when the gateway cannot read them', async ()=>{
		const { fixture, page } = create( {}, true );
		await flush( 2 );
		fixture.detectChanges();
		expect( page.rows() ).toEqual( [] );
		expect( page.error() ).toContain( 'no session' );
		expect( fixture.nativeElement.querySelector( '[role="alert"]' )?.textContent ).toContain( 'Could not read the references.' );
	} );
} );